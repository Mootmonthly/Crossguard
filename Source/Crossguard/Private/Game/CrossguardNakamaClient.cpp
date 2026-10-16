#include "Game/CrossguardNakamaClient.h"

#include "Crossguard.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FCrossguardNakamaResponse::IsRetryable() const
{
	return !bConnected || HttpStatus == 0 || HttpStatus == 429 || HttpStatus >= 500;
}

FString FCrossguardNakamaResponse::Describe() const
{
	if (!bConnected)
	{
		return TEXT("no response (timeout or connection refused)");
	}
	if (IsOk())
	{
		return FString::Printf(TEXT("HTTP %d"), HttpStatus);
	}
	return FString::Printf(TEXT("HTTP %d (code %d): %s"), HttpStatus, ErrorCode, ErrorMessage.IsEmpty() ? *Body.Left(200) : *ErrorMessage);
}

FCrossguardNakamaClient::FCrossguardNakamaClient(const FString& InBaseUrl, float InTimeoutSeconds)
	: BaseUrl(InBaseUrl)
	, TimeoutSeconds(InTimeoutSeconds)
{
	BaseUrl.RemoveFromEnd(TEXT("/"));
}

void FCrossguardNakamaClient::CallServerRpc(const FString& HttpKey, const FString& RpcId, const FString& PayloadJson, FCallback OnDone) const
{
	// ?unwrap: the body is the payload itself (not a JSON string holding it), and so is the answer.
	Post(FString::Printf(TEXT("%s/v2/rpc/%s?unwrap"), *BaseUrl, *RpcId), BasicAuth(HttpKey), PayloadJson, MoveTemp(OnDone));
}

void FCrossguardNakamaClient::CallClientRpc(const FString& SessionToken, const FString& RpcId, const FString& PayloadJson, FCallback OnDone) const
{
	Post(FString::Printf(TEXT("%s/v2/rpc/%s?unwrap"), *BaseUrl, *RpcId), TEXT("Bearer ") + SessionToken, PayloadJson, MoveTemp(OnDone));
}

void FCrossguardNakamaClient::AuthenticateDevice(const FString& ServerKey, const FString& DeviceId, FCallback OnDone) const
{
	Post(FString::Printf(TEXT("%s/v2/account/authenticate/device?create=true"), *BaseUrl), BasicAuth(ServerKey),
		FString::Printf(TEXT("{\"id\":%s}"), *Quote(DeviceId)), MoveTemp(OnDone));
}

void FCrossguardNakamaClient::Post(const FString& Url, const FString& Authorization, const FString& Body, FCallback OnDone) const
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), Authorization);
	Request->SetContentAsString(Body);
	Request->SetTimeout(TimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda(
		[OnDone = MoveTemp(OnDone)](FHttpRequestPtr /*Sent*/, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			FCrossguardNakamaResponse Response;
			Response.bConnected = bConnectedSuccessfully && HttpResponse.IsValid();
			if (Response.bConnected)
			{
				Response.HttpStatus = HttpResponse->GetResponseCode();
				Response.Body = HttpResponse->GetContentAsString();
				if (!Response.IsOk())
				{
					ParseError(Response);
				}
			}
			if (OnDone)
			{
				OnDone(Response);
			}
		});
	if (!Request->ProcessRequest())
	{
		UE_LOG(LogCrossguard, Error, TEXT("Nakama: could not start the request to %s"), *Url);
	}
}

FString FCrossguardNakamaClient::Quote(const FString& Value)
{
	FString Out;
	Out.Reserve(Value.Len() + 2);
	Out.AppendChar(TEXT('"'));
	for (const TCHAR Char : Value)
	{
		switch (Char)
		{
		case TEXT('"'):
			Out += TEXT("\\\"");
			break;
		case TEXT('\\'):
			Out += TEXT("\\\\");
			break;
		case TEXT('\n'):
			Out += TEXT("\\n");
			break;
		case TEXT('\r'):
			Out += TEXT("\\r");
			break;
		case TEXT('\t'):
			Out += TEXT("\\t");
			break;
		default:
			if (Char < 0x20)
			{
				Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
			}
			else
			{
				Out.AppendChar(Char);
			}
		}
	}
	Out.AppendChar(TEXT('"'));
	return Out;
}

bool FCrossguardNakamaClient::IsUserId(const FString& Value)
{
	if (Value.Len() != 36)
	{
		return false;
	}
	for (int32 Index = 0; Index < Value.Len(); ++Index)
	{
		const TCHAR Char = Value[Index];
		const bool bHyphen = Index == 8 || Index == 13 || Index == 18 || Index == 23;
		const bool bHex = (Char >= TEXT('0') && Char <= TEXT('9')) || (Char >= TEXT('a') && Char <= TEXT('f'));
		if (bHyphen ? Char != TEXT('-') : !bHex)
		{
			return false;
		}
	}
	return true;
}

FString FCrossguardNakamaClient::BasicAuth(const FString& Key)
{
	return TEXT("Basic ") + FBase64::Encode(Key + TEXT(":"));
}

bool FCrossguardNakamaClient::ReadTokenClaims(const FString& Token, FString& OutUserId, FString& OutUsername, int64& OutExpiresAt)
{
	TArray<FString> Parts;
	if (Token.ParseIntoArray(Parts, TEXT("."), false) != 3)
	{
		return false;
	}
	// base64url without padding -> standard base64.
	FString Claims = Parts[1].Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
	while (Claims.Len() % 4 != 0)
	{
		Claims.AppendChar(TEXT('='));
	}
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Claims, Bytes))
	{
		return false;
	}
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	const FString ClaimsJson(Converted.Length(), Converted.Get());
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ClaimsJson), Object) || !Object.IsValid())
	{
		return false;
	}
	double Expiry = 0.0;
	if (!Object->TryGetStringField(TEXT("uid"), OutUserId) || !Object->TryGetNumberField(TEXT("exp"), Expiry) || !IsUserId(OutUserId))
	{
		return false;
	}
	Object->TryGetStringField(TEXT("usn"), OutUsername);
	OutExpiresAt = static_cast<int64>(Expiry);
	return true;
}

void FCrossguardNakamaClient::ParseError(FCrossguardNakamaResponse& Response)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Object) || !Object.IsValid())
	{
		return;
	}
	double Code = 0.0;
	if (Object->TryGetNumberField(TEXT("code"), Code))
	{
		Response.ErrorCode = static_cast<int32>(Code);
	}
	Object->TryGetStringField(TEXT("message"), Response.ErrorMessage);
}
