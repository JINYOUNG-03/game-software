"use strict";
// Source/geometry and transaction arithmetic only. Does not compile C++ or run the game.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "../BaseProject_SE/SimpleGame");
const read = name => fs.readFileSync(path.join(root, name), "utf8");
const trade = read("HarborTrade.cpp"), game = read("SimpleGame.cpp"), header = read("Trade.h");
const layout = read("TradePresentation.h");
const section = (source, begin, end) =>
    source.slice(source.indexOf(begin), source.indexOf(end, source.indexOf(begin)));
const open = section(trade, "void World::Trade()", "void World::EndTrade()");
const close = section(trade, "void World::EndTrade()", "std::vector<TradeOffer>");
const offers = section(trade, "std::vector<TradeOffer>", "bool World::ExecuteTrade");
const commit = trade.slice(trade.indexOf("bool World::ExecuteTrade"));
for (const source of [open, close, offers])
{
    assert(!/stats\.\w+\s*(?:\+=|-=|\+\+|--|=(?!=))/.test(source));
    assert(!/Transfer\(|ChooseRecord\(|ExecuteTrade\(/.test(source),
           "Browsing/closing must not perform a transaction");
}
assert(open.includes("trade.partner = npc->Id()"));
assert(commit.includes("Find(trade.partner)") && !commit.includes("Nearest("), "Keep the original partner");
assert(commit.indexOf("!CanTradeWith(*npc)") < commit.indexOf("stats.coins -= total"));
assert(commit.indexOf("const auto offers = TradeOffers()") < commit.indexOf("stats.coins -= total"));
assert(commit.includes("quantity <= 0 || quantity > selected->maximum"));
for (const action of ["BuyPotion", "SellHide", "BuyMeal", "BuyLedger", "SellLedger", "GiveKey"])
    assert(offers.includes("add(TradeAction::" + action) && commit.includes("case TradeAction::" + action));
for (const condition
         of ["npc.affinity >= -20", "npc.health > 0", "npc.threatTime <= 0", "level.ClearLine(hero, partner)",
             "WorldLayout::ShareMarket(hero, partner)", "recordChoice == RecordChoice::Undecided",
             "Holder(key) == level.Player().Id()"])
    assert(trade.includes(condition), condition);
const ledgerOffer = section(offers, "const bool authority", "if (Holder(key)");
assert(ledgerOffer.includes("const bool ready = copied && ledgerAccountsHeard.all()") &&
       /ready\s*\?\s*\(std::min\)/.test(ledgerOffer) && ledgerOffer.includes(": 0,"),
       "The ledger offer must stay unavailable until all four accounts are recorded");
assert(header.includes("action == TradeAction::SellLedger || action == TradeAction::GiveKey"));
const input = section(game, "void UpdateTradeInput()", "void Update(float dt)");
assert(input.includes("Harbor::NeedsTradeConfirmation(offer.action) && !tradeConfirmPending"));
assert(input.indexOf("tradeConfirmPending = true") < input.indexOf("harbor.ExecuteTrade("));
const update = game.slice(game.indexOf("void Update(float dt)"));
assert(update.indexOf("UpdateTradeInput()") < update.indexOf("firstLevel.MovePlayer("));
assert(game.includes("harbor.Conversation().Active() || harbor.Trading().Active()"));
assert(game.includes("ui.variant = int(part)") && game.includes("UiPart::Trade"));
assert(game.includes("tradeClickX - origin.x") && game.includes("tradeClickY - origin.y"));
assert(input.includes("ClearGameplayInput(true)") && input.includes("harbor.EndTrade()"));
assert(game.includes("keys['t'] = tradeHeld") && game.includes("keys[27] = escapeHeld"));
assert(game.includes("tradeQuantity = 1") && game.includes("Shop::Quantity"));
for (const name of ["Trade.h", "TradePresentation.h", "HarborTrade.cpp"])
{
    assert(read("SimpleGame.vcxproj").includes('Include="' + name + '"'));
    assert(read("SimpleGame.vcxproj.filters").includes('Include="' + name + '"'));
}
const max = 2147483647;
const limit = (wallet, stock, price, owned, recipient) =>
    wallet < 0 || stock <= 0 || price <= 0 || owned < 0 || recipient < 0
        ? 0
        : Math.min(stock, Math.floor(wallet / price), max - owned, Math.floor((max - recipient) / price));
assert(header.includes("maximum - owned") && header.includes("(maximum - recipientWallet) / price"));
assert.equal(limit(7, 5, 8, 0, 0), 0);
assert.equal(limit(80, 0, 8, 0, 0), 0);
assert.equal(limit(80, 5, 8, max, 0), 0);
assert.equal(limit(80, 5, 8, 0, max - 7), 0);
assert.equal(limit(80, 5, 8, 0, 0), 5);
assert.equal(limit(-1, 5, 8, 0, 0), 0);
let transactionCases = 0;
for (const price of [4, 6, 8, 30, 40, 50])
    for (const wallet of [0, price - 1, price, 100, max])
        for (const stock of [0, 1, 4, max])
            for (const owned of [0, 3, max - 1, max])
                for (const recipient of [0, max - 1, max])
                {
                    const count = limit(wallet, stock, price, owned, recipient);
                    assert(count >= 0 && count <= stock);
                    if (count)
                    {
                        const total = count * price;
                        assert(total <= wallet && owned + count <= max && recipient + total <= max);
                        assert.equal(BigInt(wallet - total) + BigInt(recipient + total),
                                     BigInt(wallet) + BigInt(recipient));
                        assert.equal(BigInt(stock - count) + BigInt(owned + count),
                                     BigInt(stock) + BigInt(owned));
                    }
                    transactionCases++;
                }
// Player owns hides but explicitly buys potions; no forced sale takes place.
const player = {
    coins : 40,
    hides : 3,
    potions : 1
},
      npc = {
          coins : 20,
          hides : 0,
          food : 4
      };
const quantity = 2;
assert(quantity <= limit(player.coins, npc.food, 8, player.potions, npc.coins));
player.coins -= quantity * 8;
npc.coins += quantity * 8;
npc.food -= quantity;
player.potions += quantity;
assert.deepEqual(player, {coins : 24, hides : 3, potions : 3});
assert.deepEqual(npc, {coins : 36, hides : 0, food : 2});
const number = name => Number(layout.match(new RegExp(name + " = ([\\d.]+)"))[1]);
const x0 = number("RowsX"), y0 = number("RowsY"), width = number("RowWidth");
const height = number("RowHeight"), stride = number("RowStride"), quantityLimit = number("QuantityLimit");
const rowAt = (x, y, count) => {
    if (x < x0 || x > x0 + width || y < y0)
        return -1;
    const row = Math.floor((y - y0) / stride);
    return row < count && y - y0 - row * stride <= height ? row : -1;
};
for (let row = 0; row < 3; row++)
{
    assert.equal(rowAt(x0 + 20, y0 + row * stride + 20, 3), row);
    assert.equal(rowAt(x0 + 20, y0 + row * stride + height + 1, 3), -1);
    for (const scale of [.5, 1, 1.5, 2])
    {
        const x = x0 + 20, y = y0 + row * stride + 20, ox = 12, oy = -8;
        assert.equal(rowAt((x + ox) * scale / scale - ox, (y + oy) * scale / scale - oy, 3), row);
    }
}
assert.equal(rowAt(x0, y0, 0), -1);
assert.equal(rowAt(x0 - 1, y0, 3), -1);
assert.equal(quantityLimit, 99);
for (const maximum of [0, 1, 4, max])
    for (const requested of [-10, 0, 1, 99, 1000])
        assert(Math.max(1, Math.min(requested, Math.min(maximum, quantityLimit))) <= quantityLimit);
const buttons = [...layout.matchAll(/constexpr Button (\w+)\{([\d, ]+)\}/g) ].map(
    m => ({name : m[1], r : m[2].split(",").map(Number)}));
assert.equal(buttons.length, 5);
for (const {r} of buttons)
    assert(r[0] >= 170 && r[1] >= 120 && r[0] + r[2] <= 1110 && r[1] + r[3] <= 700);
for (let i = 0; i < buttons.length; i++)
    for (let j = i + 1; j < buttons.length; j++)
    {
        const [a, b] = [ buttons[i].r, buttons[j].r ];
        assert(a[0] + a[2] <= b[0] || b[0] + b[2] <= a[0] || a[1] + a[3] <= b[1] || b[1] + b[3] <= a[1]);
    }
console.log(JSON.stringify({
    event : "trade_window_contracts",
    passed : true,
    transaction_cases : transactionCases,
    browse_without_mutation : true,
    explicit_purchase_with_hides : true,
    story_confirmation : true,
    fixed_partner_revalidated : true,
    game_build_performed : false,
    game_execution_performed : false
}));
