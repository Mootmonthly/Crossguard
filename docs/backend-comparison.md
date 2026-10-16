# Backend and server hosting comparison

Research for choosing the online backend (accounts, saves, parties, guilds, matchmaking, store) and
the dedicated server hosting (raid and hub servers). Prices and status checked on the live pages on
**2026-10-16**; they change often, re-check before buying. Not a ruling: the choice is the owner's.
Ties in with [persistence.md](persistence.md) (`ICrossguardSaveBackend`), [parties.md](parties.md)
(matchmaker), [guild-rules.md](guild-rules.md), [steam-setup.md](steam-setup.md),
[anti-cheat.md](anti-cheat.md).

## What Crossguard needs

| Need | Notes |
| --- | --- |
| Steam login, accounts | Steam id is the player id; one account, several characters |
| Character saves | Written only by our dedicated servers (raid end, hub changes), read at join. Clients never write |
| Parties | Invites, inbox, leader; the party travels hub → raid → hub together |
| Guilds | Ranks, bank, log (rules engine already in `CrossguardRules`; needs storage) |
| Matchmaking | Whole parties into multi-team raid servers (assumed up to ~30-60 players) |
| Dedicated servers | Raid servers (short-lived, ~20-40 min) and **shared hub servers** (long-lived, sharded, many players each) |
| Later | Auction house, real-money store (season subscription ~$10, respec tokens) with server-side purchase checks |
| Constraints | Server authority, 1 owner + Claude, low ops, no lock-in that blocks growth, testable on one dev PC |

## Status check (2026)

| Service | Status on 2026-10-16 |
| --- | --- |
| **Hathora** | **Shut down 2026-05-05** (team joined Fireworks AI; Stormgate lost multiplayer modes). Do not use. |
| **Unity Multiplay** (Game Server Hosting) | **Ended 2026-03-31** for Unity; licensed to Rocket Science Group, no public pricing. Do not use. Unity Matchmaker / Lobby / Relay continue. |
| PlayFab | Operating. Development Mode free tier now **1,000 lifetime players per title**; new "Foundation Mode" (Mar 2026) is free only with an Xbox publishing deal. MPS VM prices rise ~6% on 2026-11-01. |
| Hetzner (self-host reference) | Operating, but **two price rises in 2026** (Apr: AX42 €56 → €68/mo; Jun: CCX cloud lines up to ~2.7×). Hardware is not the cheap fallback it was. |
| Nakama, GameLift, Edgegap, AccelByte, EOS, Beamable, Pragma, SpacetimeDB, Agones | Operating. Nakama v3.41.0 (Sep 2026), nakama-unreal v2.11.6 (Aug 2026, UE 5.8 fix in 2.11.5), GameLift UE plugin 3.4.0 (Aug 2026, adds UE 5.8), Agones v1.61.0 (Sep 2026). |

## What each covers

Y = built in, P = partial / needs our code, — = no.

| | Steam auth | Saves / storage | Parties | Guilds | Matchmaker | Server hosting | Store / IAP verify | Leaderboards |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **EOS** (Epic) | Y | P (player data storage, key-value files) | P (lobbies) | — | P (session search) | — | — for Steam (Ecom is Epic Store only) | Y |
| **PlayFab** | Y | Y (player data, Economy v2 inventory) | Y (Party is voice/data; Lobby for groups) | Y (Groups) | Y | Y (MPS on Azure) | Y (Economy v2 Steam redemption) | Y |
| **Nakama** (OSS / Heroic Cloud) | Y | Y (storage engine, server-only write perms, wallet + ledger) | Y (leader, invites, join requests, up to 256, matchmake as a party) | Y (groups with roles, join requests) | Y (party tickets, custom matchers) | — (FleetManager plugins: GameLift, Edgegap, i3D) | P (Apple/Google/Huawei/FB built in; **Steam: our Go RPC**) | Y |
| **GameLift Servers** (AWS) | — | — | — | — | Y (FlexMatch) | Y (managed EC2 / containers, Spot, Anywhere) | — | — |
| **Edgegap** | — | — | P (matchmaker groups) | — | Y (groups, teams, backfill) | Y (edge cloud per minute, private bare-metal fleet) | — | — |
| **AccelByte AGS** | Y | Y (cloud save, inventory) | Y | Y (guilds/clans) | Y | Y (AMS) | Y (store, wallet, payments, season pass) | Y |
| **Beamable** | Y | Y | Y | Y | Y | P (via Edgegap / partners) | Y | Y |
| **Pragma** | Y | Y | Y | ? | Y | P (partners) | Y (orders/monetization) | ? |
| **SpacetimeDB** | P | Y (it is a database with server logic) | P (write it) | P | P | — (it is the server; not for UE dedicated servers) | — | P |
| **Roll your own** (Go/Node + Postgres + Agones) | P (Steam Web API ticket check) | Y | write it | write it | write it (or Open Match) | Y (Agones on Kubernetes) | write it | write it |

## SDK, local testing, lock-in, risk

| | UE 5 / C++ support | One-PC local testing | Lock-in / exit | Maturity / risk |
| --- | --- | --- | --- | --- |
| EOS | Excellent (OSS EOS plugin ships with UE) | Needs Epic cloud | Low (free, use piecemeal) | Very mature; doesn't cover most needs |
| PlayFab | Official UE plugin (C++ / BP) | Cloud only; MPS has LocalMultiplayerAgent | **High**: proprietary data model, Azure-only MPS | Mature; free tier gutted in 2026, pushing toward Xbox |
| Nakama | nakama-unreal C++ + BP, keeps up with UE (5.8 fix Jul 2026); historically lags new engine versions a few weeks | **Yes**: Docker (Nakama + Postgres) on the dev PC, same as prod | **Low**: Apache 2.0, our Postgres data, self-host or Heroic Cloud | Mature (since 2017). OSS is **single-node** (clustering is Enterprise / Heroic Cloud) |
| GameLift | Official UE plugin + server SDK 5 (open source), UE 5.0-5.8 | **Yes**: GameLift Anywhere registers the dev PC as a fleet | Medium: a few SDK calls in the server (`InitSDK`, `ProcessReady`, accept player session); hide behind an interface | Very mature (AWS). Console/IAM complexity |
| Edgegap | Free UE integration kit on Fab, Docker-based | Server runs locally as a normal exe; no local orchestrator | Low: Docker images, thin API | Mid-size vendor; gained Hathora/Multiplay refugees |
| AccelByte | Official UE SDK + OSS plugin, strong | Cloud only | High (whole platform) | Mature, but priced for funded studios |
| Beamable | UE SDK exists, smaller user base | Partial (microservices run locally) | High | Smaller vendor, pricing unclear |
| Pragma | UE focus, no public SDK/pricing | Unknown | High | Funded (~$50M), sales-led, aimed at larger studios |
| SpacetimeDB | UE SDK in **preview**, UE 5.6 documented | Yes | Medium | Young; wrong shape for UE dedicated servers |
| Roll your own | We write the UE client | Yes (docker compose, kind/minikube for Agones) | None | We own every bug and every 3 a.m. page |

## Rough monthly cost

Assumptions (all placeholder, until we profile a real server):
- A raid server process holds up to 48 players on **2 vCPU / 4 GB**; a hub instance holds ~64 on 2 vCPU.
- At any moment ~70% of players are in raids, ~30% in hubs or menus; raids fill ~75% on average.
- Average CCU is ~60% of peak; server traffic ~**10 KB/s per player** out.
- So 1k peak CCU ≈ 52 vCPU at peak, ~23,000 vCPU-hours and ~16 TB egress a month. 10k ≈ 10×.

### Server hosting (raid + hub servers)

| | Dev / testing | ~1k CCU | ~10k CCU |
| --- | --- | --- | --- |
| **GameLift** on-demand (c6a.large $0.099/h, Ohio; bandwidth free on gen 6+) | ~$0 (Anywhere on our PC; new AWS accounts get $100-200 credits, 6 months) | **~$1.1-1.4k** | ~$11-13k (less with Spot for hubs, savings plans) |
| **Edgegap** edge cloud ($0.00115/vCPU-min + **$0.10/GB** egress) | Free trial (1 deployment, 60 min cap) | ~$3.1k (half is egress) | ~$30k |
| **Edgegap** private fleet ($350/host/mo month-to-month, $280 on 12 mo; 15 usable vCPU, 6 TB egress incl.) | — | **~$1.4k** (4 hosts, sized for peak) | ~$11-14k (+ edge cloud for spikes) |
| **PlayFab MPS** (Azure D2as_v4 list ~$0.096/h; egress $0.05-0.16/GB after 10 GB) | 750 free Dasv4 core-hours/mo for evaluation | ~$1.9k | ~$19k |
| **AccelByte AMS** (8 vCPU $0.415/h; egress ~$0.10/GB) | 1,200 h of a small dev instance | ~$2.7k | ~$27k |
| **Self-host** bare metal + Agones (Hetzner AX42 ~€68+/mo, traffic included) | $0 (local) | ~$500-700 + **our ops time**, EU-only datacenters | ~$5-7k + a real ops job |

### Backend (accounts, saves, parties, guilds, matchmaking)

| | Dev / testing | ~1k CCU | ~10k CCU |
| --- | --- | --- | --- |
| **Nakama self-host** (1 VM + managed Postgres) | $0 (Docker on PC) | **~$80-200** | ~$500-1,000 (one big node + DB replica; beyond one node: Heroic Cloud quote or our own sharding) |
| **Heroic Cloud** (managed Nakama Enterprise) | Dev tier (price not published) | Quote only (priced by Nakama + DB cores; support from $2k/mo) | Quote |
| **PlayFab** | Free (≤1,000 lifetime players/title) | ~$99-300 (Standard plan) | ~$1-3k (overage, e.g. profile writes $7.15/M; or Premium $1,999) |
| **AccelByte AGS** (per peak CCU per day, first 30 free) | Free ≤30 CCU | ~$2.2k (Multiplayer) / ~$3.2k (Complete) | ~$21k / ~$31k |
| **EOS** | Free | Free | Free (but covers little) |
| **Beamable** | 90-day trial | API-call based ($100 per 10M calls), unclear | Unclear |

## Recommendation

**Nakama (self-hosted, open source) for the backend + GameLift Servers for raid and hub servers.
Steam for login and payments. EOS optional later (voice, Easy Anti-Cheat).**

Why, for this game:
- **Nakama covers the social layer we need almost exactly**: Steam login, storage objects writable
  only by the server (our dedicated servers write saves through server-to-server RPCs), parties
  with leader / invites / join requests that matchmake as one ticket, groups with roles for guilds,
  notifications for the party/guild inbox, wallet with a ledger, leaderboards, and a Go runtime
  plus direct Postgres access for the guild bank log and the later auction house.
- **Lowest lock-in**: Apache 2.0, our own Postgres. Start self-hosted for ~$100/mo; move to
  Heroic Cloud if one node is not enough, or leave entirely with our data.
- **Whole stack runs on the dev PC**: `docker compose up` for Nakama + Postgres, GameLift Anywhere
  for the dedicated servers. Matches how we test today (local file save backend → Nakama backend
  behind the same `ICrossguardSaveBackend`).
- **GameLift over Edgegap** at our traffic shape: network egress is free on gen-6+ instances, and
  egress is half the Edgegap edge cloud bill. Official UE plugin already supports UE 5.8; Heroic
  Labs maintains the Nakama-GameLift FleetManager plugin; long-lived hub sessions and short raid
  sessions both fit; Anywhere gives a local mode. AWS will not vanish (2026 lost Hathora and
  Multiplay). Use Nakama's matchmaker, not FlexMatch, so matchmaking stays portable.
- **Keep hosting swappable**: wrap the GameLift server SDK in one interface
  (`ICrossguardServerHost`: ready, accept player, game session end). Edgegap private fleet is the
  fallback at the same price point if AWS gets painful; Agones on our own metal if we ever grow
  big enough to staff ops.
- **Payments**: no backend verifies Steam purchases for us except PlayFab/AccelByte. Steam
  microtransactions (ISteamMicroTxn, incl. recurring billing for the season subscription) are
  server-side Web API calls with our publisher key; we write them as a Nakama Go RPC. Grants go
  through the Nakama wallet/storage, never the client.

Not chosen: PlayFab all-in-one (high lock-in, Azure-only servers, free tier cut to 1,000 lifetime
players, shrinking non-Xbox focus); AccelByte (~$2-3k/mo backend fees at 1k CCU before servers);
EOS alone (no saves we control, no guilds, no hosting); Beamable / Pragma (sales-led, unclear
pricing, smaller UE user base); SpacetimeDB (preview UE SDK, replaces rather than serves UE
dedicated servers); roll-your-own (Nakama already is the good version of it, and open source).

Estimated total: **dev ~$0-20/mo**, **1k CCU ~$1.2-1.6k/mo** (servers ~$1.1-1.4k + backend
~$100-200), **10k CCU ~$12-15k/mo**. Server hosting is ~90% of the bill at every size, so profile
players-per-vCPU early: halving server cost matters far more than any backend choice.

## What to build first

1. **Nakama locally**: docker compose (Nakama + Postgres) in `Tools/`, a Go runtime module in the
   repo, Steam ticket auth (dev: device/custom ids matching our `dev-<name>` ids).
2. **`UCrossguardNakamaSaveBackend`** implementing `ICrossguardSaveBackend`: dedicated servers
   read/write character saves via server-key RPCs; storage objects with no client write permission.
   Scenario tests against the local Nakama.
3. **Parties on Nakama**: replace the in-server party with Nakama parties (invites, leader, inbox
   via notifications); `PartyStartRaid` → Nakama matchmaker ticket with the party id.
4. **Server host interface + GameLift Anywhere**: raid and hub servers register from the dev PC;
   Nakama-GameLift FleetManager places matched parties; hub shard picker (fill to N, then new
   instance) in the Go module.
5. **Guilds**: Nakama groups + our `CrossguardRules` guild rules in the Go module (ranks as group
   metadata, bank and log in Postgres tables).
6. Later: GameLift managed fleets (one region first), Steam microtransactions RPC, auction house.

## Assumptions (flag for the owner)

- (placeholder) Raid size up to ~48 players; 2 vCPU / 4 GB per raid server and ~64 players per hub
  instance; 10 KB/s per player out; average CCU 60% of peak. All cost rows scale with these.
- (placeholder) One region at launch (US East or EU); multi-region multiplies the idle floor.
- Assumed: Nakama OSS single node handles the 1k CCU launch comfortably (not load-tested by us).
- Assumed: Steam is the only store at launch (no console / Epic Store purchase paths).
- Building any Linux dedicated server target needs a source-built Unreal (true for every host).

## Sources (checked 2026-10-16)

- Hathora shutdown: <https://crux.supercraft.host/blog/hathora-shut-down-where-to-go-after-may-2026/>,
  <https://docs.edgegap.com/docs/tools-and-integrations/switch-from-hathora>,
  <https://letsdatascience.com/news/stormgate-loses-multiplayer-modes-after-hathora-sale-5c00ca2c>
- Unity Multiplay deprecation: <https://status.unity.com/info_notices/362941>,
  <https://crux.supercraft.host/blog/game-backend-shutdowns-2026-who-died-where-studios-went/>
- PlayFab pricing: <https://developer.microsoft.com/en-us/games/products/playfab/pricing/>,
  <https://learn.microsoft.com/en-us/gaming/playfab/features/economy-v2/tutorials/marketplace-redemption/steam>,
  <https://learn.microsoft.com/en-za/xbox/playfab/multiplayer/servers/billing-for-thunderhead>
- Heroic Cloud pricing: <https://heroiclabs.com/pricing>; Nakama docs:
  <https://heroiclabs.com/docs/nakama/concepts/parties/>,
  <https://heroiclabs.com/docs/nakama/concepts/iap-validation/>,
  <https://heroiclabs.com/docs/nakama/concepts/multiplayer/session-based/>,
  <https://heroiclabs.com/docs/nakama/getting-started/architecture/> (clustering is Enterprise);
  releases: <https://github.com/heroiclabs/nakama/releases>,
  <https://github.com/heroiclabs/nakama-unreal/releases>
- GameLift: <https://aws.amazon.com/gamelift/servers/pricing/>,
  <https://docs.aws.amazon.com/gameliftservers/latest/developerguide/gamelift-intro-pricing.html>,
  <https://aws.amazon.com/gamelift/pricing/flexmatch-pricing/>,
  <https://github.com/amazon-gamelift/amazon-gamelift-plugin-unreal/releases>,
  AWS free tier change: <https://aws.amazon.com/about-aws/whats-new/2025/07/aws-free-tier-credits-month-free-plan/>
- Edgegap: <https://edgegap.com/pricing>, <https://docs.edgegap.com/learn/matchmaking>,
  <https://docs.edgegap.com/learn/unreal-engine-games/getting-started-with-servers>,
  <https://heroiclabs.com/docs/nakama/guides/concepts/edgegap-integration/>
- AccelByte: <https://accelbyte.io/ags-pricing>, <https://accelbyte.io/ams-pricing>
- EOS: <https://onlineservices.epicgames.com/llms.txt>
- Beamable: <https://beamable.com/sales>; Pragma: <https://pragma.gg/>,
  <https://finder.techleap.nl/news/feed/pragma-raises-12-75m-for-game-backend>
- SpacetimeDB: <https://spacetimedb.com/pricing>, <https://spacetimedb.com/docs/sdks/unreal>
- Agones: <https://github.com/googleforgames/agones/releases>
- Steam microtransactions: <https://partner.steamgames.com/doc/features/microtransactions>
- Hetzner 2026 prices: <https://findstack.com/resources/hetzner-price-increase-2026>,
  <https://wz-it.com/en/blog/hetzner-price-increase-june-2026-cpx-ccx-alternatives/>
