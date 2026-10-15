#include "Skills/CrossguardLightOrb.h"

#include "Components/PointLightComponent.h"

ACrossguardLightOrb::ACrossguardLightOrb()
{
	bReplicates = true;
	InitialLifeSpan = 120.0f;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	RootComponent = Light;
	// Placeholder values: lights about 15 m around the caster.
	Light->SetIntensity(5000.0f);
	Light->SetAttenuationRadius(1500.0f);
	Light->SetLightColor(FLinearColor(1.0f, 0.95f, 0.8f));
	Light->SetCastShadows(false);
}
