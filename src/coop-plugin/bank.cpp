// SW Bank: a team's shared cats on the world server. The bank box (the ATM: the Cat Vault building) opens an amount box;
// DEPOSIT takes the cats from this player's purse first and sends them, WITHDRAW asks the server and pays out on its yes.
// The server decides and saves before it answers (store_main.cpp kUpBank); a refused deposit is refunded here.
#include "bank.h"
#include "team.h"
#include "coop_log.h"
#include "../common/teamwire.h"
#include "../common/panelstatus.h"
#include "../common/playerstab.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <Windows.h>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

namespace coop {
int BankPurseMoney();                   // resurrect.cpp: the player's cats, -1 no purse
int BankPurseTake(int amount);          // resurrect.cpp: 1 paid, 0 short, -1 no purse; a negative amount gives
bool TeamSendRaw(const std::vector<char>& b);   // team.cpp

namespace {
const char* const kBankBuildingSid = "5007295-Cat Pouch.mod";   // the Cat Vault (Cat Pouch mod) is the ATM
MyGUI::Widget* g_win = 0;
MyGUI::EditBox* g_amount = 0;
MyGUI::EditBox* g_info = 0;
unsigned g_balance = 0;
int g_balanceKnown = 0;
unsigned g_nextTxn = 1;
std::map<unsigned, unsigned> g_deposits;   // txn -> cats taken from the purse, waiting for the server
std::map<unsigned, unsigned> g_withdraws;  // txn -> cats asked for
std::string g_note;

std::string S(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string Cats(unsigned v)
{
    std::string d = S(v), out;
    for (size_t i = 0; i < d.size(); ++i) { if (i != 0 && (d.size() - i) % 3 == 0) out += ','; out += d[i]; }
    return "c." + out;
}
MyGUI::UString U(const std::string& s) { return MyGUI::UString(playerstab::EscapeHash(s).c_str()); }
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }
int Purse() { return BankPurseMoney(); }

void Refresh()
{
    if (g_info == 0) return;
    const int p = Purse();
    std::string t = "Team bank: " + std::string(g_balanceKnown ? Cats(g_balance) : "asking the world server...") + "\n";
    t += "Your cats: " + std::string(p >= 0 ? Cats((unsigned)p) : "-") + "\n";
    if (!g_note.empty()) t += "\n" + g_note;
    g_info->setCaption(U(t));
}
unsigned AmountTyped()
{
    if (g_amount == 0) return 0;
    std::string s = g_amount->getOnlyText().asUTF8();
    unsigned long long v = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (unsigned)(s[i] - '0'); if (v > swteam::kMaxBank) return swteam::kMaxBank + 1; } }
    return (unsigned)v;
}
void Close()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (g_win != 0 && gui != 0)
    {
        MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
        if (im != 0) im->removeWidgetModal(g_win);
        gui->destroyWidget(g_win);
    }
    g_win = 0; g_amount = 0; g_info = 0;
}
void Deposit()
{
    const unsigned n = AmountTyped();
    if (n == 0 || n > swteam::kMaxBank) { g_note = "Type how many cats."; Refresh(); return; }
    const int r = BankPurseTake((int)n);
    if (r != 1) { g_note = r == 0 ? "You don't have " + Cats(n) + "." : "No purse to take from."; Refresh(); return; }
    std::vector<char> b; const unsigned txn = g_nextTxn++;
    if (!swteam::EncodeBank(&b, swteam::kBankDeposit, txn, n) || !TeamSendRaw(b))
    { BankPurseTake(-(int)n); g_note = "Not connected - nothing deposited."; Refresh(); return; }
    g_deposits[txn] = n;
    g_note = "Depositing " + Cats(n) + "...";
    DebugLog("[BANK] deposit " + S(n) + " sent (txn " + S(txn) + "), taken from the purse");
    Refresh();
}
void Withdraw()
{
    const unsigned n = AmountTyped();
    if (n == 0 || n > swteam::kMaxBank) { g_note = "Type how many cats."; Refresh(); return; }
    std::vector<char> b; const unsigned txn = g_nextTxn++;
    if (!swteam::EncodeBank(&b, swteam::kBankWithdraw, txn, n) || !TeamSendRaw(b)) { g_note = "Not connected."; Refresh(); return; }
    g_withdraws[txn] = n;
    g_note = "Withdrawing " + Cats(n) + "...";
    DebugLog("[BANK] withdraw " + S(n) + " asked (txn " + S(txn) + ")");
    Refresh();
}
void SafeRefresh() { __try { Refresh(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
void OnDeposit(MyGUI::Widget*) { __try { Deposit(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
void OnWithdraw(MyGUI::Widget*) { __try { Withdraw(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
void OnClose(MyGUI::Widget*) { __try { Close(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

MyGUI::Widget* Mk(MyGUI::Widget* parent, const char* type, const char* skin, int l, int t, int w, int h)
{ return parent->createWidgetT(std::string(type), std::string(skin), MyGUI::IntCoord(l, t, w, h), MyGUI::Align::Default); }

bool Open()
{
    if (g_win != 0) return true;
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return false;
    int vw = 1280, vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { vw = s.width; vh = s.height; } }
    const int w = 440, h = 270;
    MyGUI::Widget* raw = 0;
    static const char* const kLayers[2] = { "Info", "Popup" };
    for (int k = 0; k < 2 && raw == 0; ++k)
    {
        raw = gui->createWidgetT(std::string("Window"), std::string("Kenshi_WindowC"), MyGUI::IntCoord((vw - w) / 2, (vh - h) / 2, w, h),
                                 MyGUI::Align::Default, std::string(kLayers[k]), std::string("SWBankBox"));
        if (raw != 0 && raw->getLayer() == 0) { gui->destroyWidget(raw); raw = 0; }
    }
    MyGUI::Window* win = raw != 0 ? raw->castType<MyGUI::Window>(false) : 0;
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) { if (raw != 0) gui->destroyWidget(raw); return false; }
    win->setMovable(true);
    win->setCaption(MyGUI::UString("BANK"));
    const int W = cl->getWidth(), H = cl->getHeight();
    MyGUI::Widget* iw = Mk(cl, "EditBox", "Kenshi_WordWrapEmpty", 10, 8, W - 20, H - 100);
    MyGUI::Widget* aw = Mk(cl, "EditBox", "Kenshi_EditBox", 10, H - 86, W - 20, 30);
    const int bw = (W - 40) / 3;
    MyGUI::Widget* d = Mk(cl, "Button", "Kenshi_Button2", 10, H - 46, bw, 36);
    MyGUI::Widget* x = Mk(cl, "Button", "Kenshi_Button2", 20 + bw, H - 46, bw, 36);
    MyGUI::Widget* c = Mk(cl, "Button", "Kenshi_Button2", 30 + 2 * bw, H - 46, bw, 36);
    g_info = iw != 0 ? iw->castType<MyGUI::EditBox>(false) : 0;
    g_amount = aw != 0 ? aw->castType<MyGUI::EditBox>(false) : 0;
    MyGUI::Button* bd = d != 0 ? d->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* bx = x != 0 ? x->castType<MyGUI::Button>(false) : 0;
    MyGUI::Button* bc = c != 0 ? c->castType<MyGUI::Button>(false) : 0;
    if (g_info == 0 || g_amount == 0 || bd == 0 || bx == 0 || bc == 0) { gui->destroyWidget(raw); g_info = 0; g_amount = 0; return false; }
    g_win = raw;
    g_info->setEditStatic(true); g_info->setEditReadOnly(true); g_info->setEditMultiLine(true); g_info->setEditWordWrap(true);
    g_info->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top); g_info->setTextColour(Lit());
    g_amount->setMaxTextLength(10); g_amount->setTextColour(Lit()); g_amount->setCaption(MyGUI::UString("1000"));
    bd->setCaption(MyGUI::UString("DEPOSIT")); bd->setTextColour(Lit());
    bx->setCaption(MyGUI::UString("WITHDRAW")); bx->setTextColour(Lit());
    bc->setCaption(MyGUI::UString("CLOSE")); bc->setTextColour(Lit());
    bd->eventMouseButtonClick += MyGUI::newDelegate(OnDeposit);
    bx->eventMouseButtonClick += MyGUI::newDelegate(OnWithdraw);
    bc->eventMouseButtonClick += MyGUI::newDelegate(OnClose);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->addWidgetModal(raw); im->setKeyFocusWidget(g_amount); }
    g_note.clear();
    Refresh();
    std::vector<char> b;
    if (swteam::EncodeBank(&b, swteam::kBankAsk, 0, 0)) TeamSendRaw(b);
    DebugLog("[BANK] box opened");
    return true;
}
} // namespace

bool BankIsAtm(const char* sid) { return sid != 0 && std::string(sid) == kBankBuildingSid; }

void BankOpen() { __try { Open(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

void BankArrived(unsigned op, unsigned txn, unsigned result, unsigned balance)
{
    g_balance = balance; g_balanceKnown = 1;
    static const char* kWhy[] = { "", "you're not in a team", "the bank doesn't have that much", "the bank is full", "the world server couldn't save", "a bad request" };
    const char* why = kWhy[result <= 5 ? result : 5];
    if (op == swteam::kBankDeposit)
    {
        std::map<unsigned, unsigned>::iterator it = g_deposits.find(txn);
        const unsigned n = it != g_deposits.end() ? it->second : 0;
        if (it != g_deposits.end()) g_deposits.erase(it);
        if (result == swteam::kBankOk) g_note = "Deposited " + Cats(n) + ".";
        else { if (n != 0) BankPurseTake(-(int)n); g_note = "Deposit refused (" + std::string(why) + ") - " + Cats(n) + " given back."; }
        DebugLog("[BANK] deposit txn " + S(txn) + " " + (result == swteam::kBankOk ? "OK" : std::string("REFUSED: ") + why) + ", bank " + S(balance));
    }
    else if (op == swteam::kBankWithdraw)
    {
        std::map<unsigned, unsigned>::iterator it = g_withdraws.find(txn);
        const unsigned n = it != g_withdraws.end() ? it->second : 0;
        if (it != g_withdraws.end()) g_withdraws.erase(it);
        if (result == swteam::kBankOk && n != 0) { BankPurseTake(-(int)n); g_note = "Withdrew " + Cats(n) + "."; }
        else if (result != swteam::kBankOk) g_note = "Withdraw refused: " + std::string(why) + ".";
        DebugLog("[BANK] withdraw txn " + S(txn) + " " + (result == swteam::kBankOk ? "OK" : std::string("REFUSED: ") + why) + ", bank " + S(balance));
    }
    SafeRefresh();
}
} // namespace coop
