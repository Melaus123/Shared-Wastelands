#pragma once
// SW Bank: the team's shared cats (bank.cpp)
namespace coop {
bool BankIsAtm(const char* buildingSid);   // the building that opens the bank box
void BankOpen();                           // main thread
void BankArrived(unsigned op, unsigned txn, unsigned result, unsigned balance);   // a BANK answer from the world server
} // namespace coop
