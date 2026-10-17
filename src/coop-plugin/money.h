// money.h - T-810: PLAYERS GIVE EACH OTHER MONEY (owner decisions 858 and 861). The road, the pp.money record format and the
// words are src/common/moneywire.h; the GIVE MONEY button and box are playerstab.cpp's; the balance, the pp.money record that
// carries the ledger and its writer are store.cpp's (StoreMoney*). This file runs the steps between them.
#pragma once

#include <string>
#include <vector>

namespace coop {

// Every in-world frame (coop.cpp detour_mainLoop): once this game's money may change (StoreMoneyReady), acknowledged rows leave
// the outbox, refused money is given back, delivered money is added, and what is on disk is sent - MONEY_SEND for each outbox
// row once per link, MONEY_APPLIED for the applied number. MAIN THREAD.
void MoneyTick();

// MONEY_SENT_OK / MONEY_DELIVER / MONEY_REFUSE from the world server (store.cpp's drain): noted in this file's tables only;
// MoneyTick acts on them. MAIN THREAD.
void MoneyArrive(unsigned type, const std::vector<char>& payload);

// The GIVE MONEY box (playerstab.cpp). MoneyHave: 1 = *money is this player's money now, else 0. MoneySend: SEND pressed for
// `slot` (named `name` in the chat line) with the box's digits, everything read live: 1 = the money was taken and the transfer
// is on its way (the chat line said so); 0 = refused, nothing changed, *hint = the box's line saying why. MAIN THREAD.
int MoneyHave(int* money);
int MoneySend(int slot, const std::string& name, const std::string& typed, std::string* hint);

}
