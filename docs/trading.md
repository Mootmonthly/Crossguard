# Trading

Player-to-player trading. Rules: `Source/CrossguardRules/Public/CrossguardRules/Trade.h`
(`Crossguard::FTradeSession`, unit tested). Game: `UCrossguardTradeComponent` on every character
(players only may trade), `UCrossguardInventoryComponent::ExchangeItems`.

Trading needs no accounts or backend: both players are in the same game, and the server swaps the
items. (Guilds do need them; see [guild-rules.md](guild-rules.md).)

## Flow

1. **Ask**: a player asks another player within 5 m. Both must be alive and not already trading or
   asked. The request waits 30 s.
2. **Accept or decline**: accepting opens the trade window on both screens.
3. **Offer**: each side puts stacks from their inventory on the table (whole stacks; not worn items
   and not the item in hand), up to 12 stacks, plus banked gold between raids. In a raid, carried
   gold is gold coins: offer them as items. Each side sees copies of the other's items, affixes
   included; nobody else sees the offers.
4. **Lock**: each side locks their offer. Changing an offer (only possible unlocked) unlocks both
   sides, so nobody agrees to a deal that changed under them. The same happens if an offered stack
   changes on its own (used up, topped up by a pickup, dropped): the offer follows the stack and
   both sides unlock. Unlocking takes back both acceptances.
5. **Accept**: with both offers locked, each side accepts. When both have, the server checks that
   both are still alive and close, that every offered stack is still as offered, that the gold is
   still banked (and fits the receiver's), and that each side has room for what they get once their
   own offer is gone, then swaps everything at once. Otherwise
   nothing changes and the trade ends (`NoRoom`).

The trade ends when either side cancels or declines, moves more than 5 m away, dies or leaves, or
the request runs out. Both sides are told why (`ECrossguardTradeEnd`).

## Assumptions

| Assumption | Value | Where |
| --- | --- | --- |
| Trading distance | 5 m, to start and throughout | `FTradeConfig::MaxDistanceCm` |
| Request timeout | 30 s | `FTradeConfig::RequestSeconds` |
| Most stacks per offer | 12 | `FTradeConfig::MaxOfferedStacks` |
| Trading in raids | Allowed (items only; banked gold between raids only) | `FTradeSession::SetOffer` |
| Received items | Placed anywhere they fit (stacks top up first), like a pickup | `ExchangeItems` |

## Testing

The placeholder trade window (T; [ui.md](ui.md)) drives the same requests. Console commands on the
character (the ` key; results in the Output Log): `TradeItems` lists your
item ids, `TradeNearest`, `TradeRespond 1`, `TradeOffer "3,7" 0`, `TradeLock 1`, `TradeAccept`,
`TradeCancel`. See [next-build-checklist.md](next-build-checklist.md).
