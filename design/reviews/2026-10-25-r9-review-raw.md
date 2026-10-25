# Round-9 review (27e19e5 vs origin/main)

Scope: `git diff origin/main...27e19e5`. I read the code and ran a separate copy, built in the scratchpad:
- 15-year games with option 0 always chosen: seeds 3, 9 and 11, through a small harness linked to `libsimcore.a`.
- A 60-year AI run on seeds 5 and 9, against the same harness built from origin/main.
- 15-year `play` runs: seeds 3 and 11.

I did not run scripts/test.sh, and I edited no code.

Line numbers refer to the files at 27e19e5.

---

## HIGH

### H1. The per-generation card cap silently cancels cards the rules depend on
`src/events.cpp:93-97`. The cap lives in `EventRunner::eligible()`. `eligible()` also gates `fireScheduled()`, and therefore every `runEventById()`. When `eligible()` is false, `fireScheduled` only writes the quiet log and returns. Nobody resolves the card, not even the AI, so its consequence simply does not happen.

The cap is keyed by `ev.id` and is `2 × variants`. It exempts only `institution`. These events fall under it:

| Event | Called from | Cap per generation | What breaks |
|---|---|---|---|
| `entry_fine_due` | contest.cpp:1073 | 2 (no variants) | One fine already takes two cards: beg, then borrow or forfeit. A third card in the generation is dropped. `resolveFine` then re-pushes the fine every month. The log repeats 「入地費…期限到了」 each month, the fine is never forfeited, and it stays in `grainFloor` and in the rival's `weak_fine` forever. |
| `debt_reckoning` | arcs.cpp:747 | 2 | After two reckonings in a generation, a third run of lord arrears is never reckoned. `debtYears >= 3` calls the suppressed card again every year. |
| `debt_steward`, `debt_neighbour` | arcs.cpp:718, economy.cpp | 2 | A step of the debt ladder is skipped. |
| `empty_larder_dire`, `hard_winter` | — | 6 and 2 | These are `"repeat": "free"` state cards, which defs.h:91 says must "answer a state as often as it comes". Once capped, a starving house gets no card and nothing happens. |
| `jury_duty`, `funeral_invite`, `widow_pressed`, `spouse_learns_affair` | — | — | Same mechanism. |

The cap also counts every time a card is shown, including when the chooser returns -1.

**Fix:** apply the cap only to pulse-fired story cards. Exempt `on.signal == "manual"`, `repeat == "free"` and signal-driven cards. Better still, when a card is capped, resolve it by the AI (`picked_ = false`) instead of skipping it. Add a test that fires `entry_fine_due` and `debt_reckoning` three times in one generation and checks the fine or debt is resolved each time.

### H2. Grain pledges forfeit on the wrong Michaelmas
Code: `src/exchange.cpp:446` sets `due = year() + (month() >= 2 ? 1 : 0)`. The text `short.pledge` and `short.done_pledge` always say 「明年米迦勒節前」. `addDebt` (`src/arcs.cpp:449`) sets the debt's own due date with `debtYears = month() >= 2 ? -1 : 0`.

- **Jan–Feb loan** (the 「1336 冬末」 scene): the debt and the pledge both fall due at *this* year's Michaelmas. The card said next year, so the land is lost a year earlier than promised.
- **Oct–Dec loan:** the debt is due at the Michaelmas after next. `debtText` says 「後年米迦勒節到期」 and `debt_neighbour` is first asked then. The pledge, however, forfeits at next Michaelmas, a year before the debt is even due, and with no debt card asked.
- Separately, `settlePledges` forfeits the whole parcel if any `owed > 0.5` is left. Paying 270 of 273 pence still loses the field.

**Fix:** derive `pl.due` from the debt's actual due year (the same arithmetic as `debtText`), and print that year in the text. Consider forfeiting only when at least half is still owed, or let the player pay the remainder on the Michaelmas screen.

### H3. 買到收成 starves the family silently to keep the floor
`src/economy.cpp:696-718`. With rule "harvest", `keepBack = grainFloor` is applied to *every* buy, including the month-at-a-time fallback.

Failure scenario:
1. The purse is at or below rent plus fine, for example 70 pence with a rent of 72.
2. `buy()` buys 0.
3. `b < daily*3` returns true. Nothing is logged, because `h.money < daily*5*price` is false.
4. `ensureFood` skips its own buy.
5. In a year let run (`routineByRule_`) the grain card is never asked (line 700).

The family eats the barn down to zero holding pence it will not spend. The new test (`tests/tests.cpp:516`) checks the purse kept the floor with the barn at 10 days, and never checks food.

**Fix:** keep the floor only for the bulk 「到收成」 buy. Let the monthly survival buy dip into it and say so in the log, or at least log the refusal and fall through to the hunger cards. Change the test to assert that the family still has a month of bread.

### H4. The rival strike plus 趁虛而入 strip land too easily
Code: `src/exchange.cpp:593-676`, `data/events/mourning.json` `widow_rival`.

**The weakness test is too loose:**
- `weak_death` is set by any death in the house within 90 days, infants included (`life.cpp:341`), whatever the purse holds.
- Seed 11, always-0: the rival bought 1 acre at 38 pence while the family had **967 pence** in hand.

**The parcel choice:**
- If the rival wants a parcel, he gets that one at any size, free land included.
- Otherwise he takes the *first* customary parcel under 6 acres, not the smallest one the comment promises.
- The only guard is `held >= 2`. There is no "can the house still feed itself" check like `ask.cpp`'s `keepsHouse` hard-no.

**Option order:** option 0 is 賣. The AI default is to refuse, but a player clicking the first line sells.

**They stack:** 趁虛而入 (`widow_rival`) fires on the same moment, a recent death, and its option 0 is also 賣 2 畝.

Measured, seed 3, always-0:
- 1340: the rival bought 5 acres for 192 pence (worth 320), taking the family from 19 to 14 acres.
- 1342: 趁虛而入 took 2 more, down to 12.
- 1344: 謀地 took 5, down to 7 by 1345.

**Fix:**
- Put the refusal first.
- Count `weak_death` only for the head, the spouse or a grown worker, and only when money is below the cost to harvest.
- Skip the strike when `keepsHouse(acres - pc.acres) < need` or the parcel is more than 25% of the holding.
- Make the strike and `widow_rival` exclusive: either `widow_rival` targets the rival and sets `struck`, or the strike skips if a land-sale card was asked within 12 months.

---

## MEDIUM

### M1. Grain loans are not bounded by what the house can repay, and the rival is dearer than the corn dealer
Code: `src/exchange.cpp:289` (give up to 120 days) and `src/exchange.cpp:309` (rival ×1.5).

- **The rival is dearer than the merchant.** A neighbour loan costs 0.375 pence per bread-day (0.6 × 1.25 × 0.5). The rival's costs 0.5625, which is above the dealer's 0.5. This contradicts `short.situation` 「糧商也賣，只是比求人貴」 and the design note 「糧商：1.5 倍」. That is the reported "273 pence for 120 days", at about 4 bread a day.
- **Ordinary loans can still bury a house.** Seed 11: 96 days for **331 pence**, lent to a 14-mouth house with 5 acres and 143 pence. That is more than two years' rent.
- **The debt piles up.** Each harvest year adds another loan to the same creditor, and the rival is always first on the card (score +100).

**Fix:** make the rival's terms worse in kind (the child, the pledge), not in price; keep its price at or below the merchant's. Cap the amount owed at, say, two years' rent or the house's yearly surplus from `keepsHouse`. Cap loans at about 60 days.

### M2. A famine 「借」 can be a free gift that the text calls a loan
`src/exchange.cpp:536-559`. Lend is always offered, but `if (room) addDebt(...)`. `debtRoom` is often false: any asker with one neighbour debt and no manor debt has no room (`arcs.cpp:465-472`). The grain leaves the barn, no debt is booked, and the option and the log (`famine.done_lend`) both say 「秋收後還 {owed} 便士」.

**Fix:** when there is no room, show lend as unavailable with the reason, or reword it and log 「這筆不記帳」.

### M3. Vote deals that don't bind, or pay twice
- **Grain card (`exchange.cpp:316`):** the vote offer does not check whether the family runs its own candidate for that office. `countVotes` gives the house's own candidate `1e6` (`contest.cpp:251`), so the promise is void and nothing records it as broken. The grain was still taken.
- **Wrong voter:** the promise is keyed to `actingHead()` (`exchange.cpp:451,862`). `countVotes` casts the house's ballot as `households_[].head` and skips heads under 21 (`contest.cpp:244-248`). With a minor or regent head, the debt is forgiven or the grain taken for a vote that is never cast.
- **Michaelmas deal pays twice (`exchange.cpp:881`):** the creditor forgives up to 20 pence *and* `favourLine(o.from, head, 4)` makes him owe the family a favour of weight 4. That is exactly the `owesUs >= 4` threshold that turns him into a free 「favour」 grain source on the next grain card (`exchange.cpp:310`).
- **Grain vote deal charges twice (`exchange.cpp:462`):** a vote deal also books `favourLine(head, f.who, 12)` on top of the vote.

**Fix:**
- Skip vote offers when the family has a candidate or a want for that office.
- Key promises to `playerHead()` and check age ≥ 21.
- Drop the extra favour from settled trades, or make it a small mutual-opinion change only.

### M4. 抵押 can take any parcel the creditor wants
`src/exchange.cpp:256`. `pledgeable(wanted)` returns the wanted parcel without the "≤4 acres, customary, not the only parcel" checks applied to every other parcel. A rival who wants your 8-acre free field can take it as the pledge for a few weeks of grain, and per H2 a penny short forfeits it.

**Fix:** apply the same checks to the wanted parcel.

### M5. Pledges and leases are keyed to a person, not a house
`exchange.cpp:446,657,685-701`. When the creditor dies, `kill()` hands his debts to the new head (`life.cpp` ~470). `h.creditor == pl.creditor` then fails, `owed = 0`, and `settlePledges` logs 「【抵押】欠 … 的債還上了，…贖了回來」 although nothing was paid. A lease whose parcel has moved to another house never comes back (`pc.holder == credHouse` fails) and is dropped.

**Fix:** store the creditor's household and compare debts by household.

### M6. 債主家的女兒 grants grace to whichever creditor the family owes
`data/events/family.json:67`.
- The target condition is only a *memory* of a past `lent_money`. The girl's house need not be the current creditor at all.
- Option 0's `debt_grace` sets `h.debtYears = 0` on the main debt, which may be the manor's. Marrying a past lender's daughter resets the steward's ox and court ladder.
- The prompt's 「這筆人情和債到現在還壓著」 can be false.

**Fix:** require her house head to be `h.creditor`, using an `owes_target`-style condition on the target, and apply grace only to that debt.

### M7. The calibration probably makes the village rich again
I ran a 60-year AI game with the same harness on both builds (one seed each, so indicative only):

| | r9 | main |
|---|---|---|
| Seed 5, median house money | 1.6–3.4k | 0.3–1.3k |
| Seed 5, median food days | 350–480 | 200–340 |
| Seed 9, median house money | 0.95–1.9k | 0.37–1.3k |
| Seed 9, median food days | 275–392 | 71–188 |

On seed 9 the player's house holds **1,600–1,900 days** of grain from about 1350 on, with 33–55 acres. Grain cards and famines then mostly stop mattering to the player. The new test (`tests.cpp:2493`) checks only a lower bound (`median >= 1.0`).

**Fix:** add an upper bound, for example median cover ≤ 1.3. Consider `kAcreBread` around 145, or spoilage on grain above about 2 years.

### M8. The cap counts by event, not by text
`events.cpp:94`. The cap is `2 × (prompts + promptIf)` per `ev.id`. If a `prompt_if` branch rarely holds, the base text can appear up to `2 × variants` times. With 3 prompts plus 3 conditional prompts, that is up to 12 identical showings, which still fails acceptance item 6. The 15-year test (seed 118, (title, situation) ≤ 2) only passes by luck of the seed.

**Fix:** count by the text actually shown (`toldPrompts_`), not by `ev.id`.

---

## LOW

### L1. Gifts of grain destroy it instead of moving it, and the change is not booked
These options remove grain the text says goes to others; nobody receives it:
- `reeve_campaign` [2] 「給村裡缺糧的幾家各送一袋麥子」
- `funeral_invite` [4] and `wedding_invite` [4] 「扛一袋麥子去…」
- `path_school` [2] 「學費拿一袋麥子抵」

They use `food_days`, which is also not on the books (`events.cpp:1626` has no `bookFood`), against acceptance item 9.

**Fix:** move the grain to the target's house (as `feed_from_target` does), and book it.

### L2. `hard_winter` [3] costs more than it says
The text says 「把耕牛賣了換糧（得 40 便士…）」. The effect is `sell_ox` then `buy_food_days: 60`, which spends the whole purse (about 150 pence at 5 bread a day), not just the 40.

### L3. `debt_neighbour` options promise things the effects don't do
- [2] 「賣牛還債（抵 40 便士）」: `sell_ox` cuts the debt by 40, but the creditor receives nothing. That is 40 pence destroyed.
- [4] 「這份情就算用掉了」 and `land_offer` [2] 「這份情就算還了」: nothing consumes the memory, so the option repeats every year.
- [1] 「成不成看{target_pron}心情」 is a flat 50%; opinion plays no part.

### L4. `village_gossip` [1] promises a favour it never books
The text says 「{target_pron}欠你一份情」. The effect is only an opinion change of +10. No `oweFavour`, which is the currency this round's cards run on.

### L5. `friend_help` [1] contradicts itself
The text says 「兩家換工，誰也不欠誰」, but the option still records the `helped_harvest` deed against the family.

### L6. The rival's lease mishandles entry fines
`exchange.cpp:651-655`. It erases *all* the house's fines but pays only the last one; any other fine just vanishes. The lord's house receives the money without a `book` entry.

### L7. 幫傭換糧 bookkeeping
- The card says 「給了 58 天的糧」 but the ledger shows 「存糧 +65 天」, because consumption drops when the child leaves (seen in seed 3).
- `sendToService`'s return value is ignored.
- The `grain_service` flag is not cleared by `recallServants` (`manor.cpp:107-117`), so any later ordinary service of that child pays no wage.

### L8. The floor is dropped after the card
`exchange.cpp:398`. After any answer to the grain card, `grainChoice = 1` puts the rest of the year on "monthly" with `keep = 0`, so the 買到收成 floor is gone for that year.

### L9. 三倍價 isn't always three times
The famine 「三倍價賣」 sells at `min(3× market, the asker's purse)`, which can be as low as 1.5×. `famine.done_sell` still says 「照三倍的價錢」.

### L10. The Michaelmas screen mislabels a loan that isn't due yet
A neighbour loan with `debtYears = -1` is shown as 「米迦勒節到期」 (`exchange.cpp:765`) although it is not due this year.

### L11. The reeve's vote-offer candidate pick
`exchange.cpp:814`. Because of operator precedence, the reeve's own house man is overwritten by any later candidate the reeve likes more.

### L12. The two clients differ
- **■ 大事:** the browser shows the newest top season and the one before, forever, even years later (`index.html` `newestTop - 1`). textplay shows everything since its cursor.
- **Free day:** the browser button's title says 「用一段空閒」 on the standing actions, and textplay's header says 「每件事用一段」, but 賣餘糧 and 拿錢還債 take no free day.

### L13. Tests that pin the bug or drift
- `tests.cpp:516` codifies H3: it checks the purse kept the floor while the barn sits at 10 days, and never checks food.
- The cap test does not cover must-run cards (H1).
- Several seed-58 cases were moved to `grownHouse()`, and the hire search gained seeds 1 and 4, because the calibration changed the prerun. That is acceptable, but those cases now test different houses.
- The calibration test has no upper bound (M7).

---

## Checked and found OK
- Grain moving between houses in 缺糧 and 荒年 is booked on both sides where it matters. Sale money is booked.
- `payCreditor` and the Michaelmas pay option move money to the right house and clear the right slot.
- 拿錢還債 and 賣餘糧 are rebuilt from the current state when pressed. A stale button is refused (it is not in the `lastAgenda_` path's "still" list), so they don't bypass the money or creditor checks.
- Promises are erased after each election (`contest.cpp:465`, `social.cpp:142`). Michaelmas skips offices already promised.
- Reputation now moves only on public acts here: 濟貧箱 −1, gift +6, gouging −6. A loan moves none.
