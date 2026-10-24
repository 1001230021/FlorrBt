// Compile selected production methods with minimal entities. No live server,
// database connection, real account data or undefined-behavior probe is used.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const read = p => readFileSync(path.join(root, p), "utf8");
function extract(text, marker, suffix = "") {
  const start = text.indexOf(marker), body = text.indexOf("{", start);
  assert.ok(start >= 0 && body >= 0, marker);
  let depth = 1, end = body + 1;
  while (depth && end < text.length) { if (text[end] === "{") depth++; if (text[end] === "}") depth--; end++; }
  assert.equal(depth, 0); return text.slice(start, end) + suffix;
}
const controller = read("src/Server/Game/controllers/player_controller.cpp");
const drops = read("src/Server/Game/entities/drop.cpp");
const petals = read("src/Server/Game/entities/petals/petal.cpp");
const mobs = read("src/Server/Game/entities/mob.cpp");
const states = read("src/Server/Game/states/states.cpp");
const mysql = read("src/Server/Persistence/mysql_account_backend.cpp");
const queue = ["void CPlayerController::PushOperate(", "bool CPlayerController::PushOperate(",
  "bool CPlayerController::ScheduleOperate(", "bool CPlayerController::IsNewerSequence(",
  "void CPlayerController::MergeOperate(", "void CPlayerController::ClearPendingOperates(",
  "void CPlayerController::ResetOperate("];
const queueTick = controller.slice(controller.indexOf("void CPlayerController::OnTick("), controller.indexOf("    // Input axes")) + "}\n";
const directory = path.join(root, "artifacts/test-server-lifecycle");
mkdirSync(directory, { recursive: true });
writeFileSync(path.join(directory, "checks.cpp"), `
#include "Server/Game/controllers/player_controller.h"
#include "Server/Persistence/account_store.h"
#include "Shared/swept_collision.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
void check(bool ok,const char* message) { if(!ok) {std::fprintf(stderr,"FAIL %s\\n",message);std::exit(1);} }
class CEntity { public: virtual ~CEntity()=default; florrbt::Vector2f pos{}; florrbt::Vector2f Pos() const {return pos;} };
class ICrabDashMob { public: virtual ~ICrabDashMob()=default;
 virtual bool IsDashActive()const{return false;} virtual bool IsDashRecovering()const{return false;} };
class CMobBase : public CEntity { public: bool attack=false,defend=false; int team=0,move=0; int& Team(){return team;}
 bool IsMovementControlledBySkill()const{return false;} };
void CPlayerController::CaptureSnapshot(CSnapshotWriter&) const {}
bool CPlayerController::RestoreSnapshot(const CSnapshotReader&,std::uint32_t,std::string&) {return true;}
void CPlayerController::ExecuteOperate(const ClientOperate& op,CMobBase* mob) {
 if(op.is_attacking) mob->attack=*op.is_attacking;
 if(op.is_defending) mob->defend=*op.is_defending;
 if(op.move_x) mob->move=*op.move_x;
}
${queue.map(m=>extract(controller,m)).join("\n")}
${queueTick}
class CPlayer {
 public: std::string account,progress; int id=1; bool authenticated=true;
 bool IsAuthenticated() const {return authenticated;}
 const std::string& GetAccountName() const {return account;}
 bool HasIsolatedProgress() const {return !progress.empty();}
 const std::string& GetProgressAccountName() const {return progress.empty()?account:progress;}
 std::uint32_t GetId() const {return id;}
};
class CGameContext {public: std::vector<std::unique_ptr<CPlayer>> players;
 const auto& Players() const {return players;} };
${extract(read("src/Server/Game/entities/mob.h"),"struct CDamageData",";")}
${extract(mobs,"CPlayer* CDamageData::ResolvePlayer(")}
enum class EEntityRemovalReason {Consumed,Despawned};
constexpr int drop_owner_all=-1;
struct Grid {std::vector<CEntity*> entities;
 template<class Fn> void ForEachInRange(florrbt::Vector2f p,float r,Fn fn) {
  for(auto* e:entities) if(Length(e->Pos()-p)<=r) fn(e);
 }};
class CGameWorld {public: Grid grid; Grid& GetSpatialGrid(){return grid;} };
class CDrop : public CEntity {
 public: CGameWorld* world=nullptr; int id=0,m_owner_id=1;
 std::string m_owner_account,m_owner_progress_account; PetalType m_type=PetalType::Basic; ERarity m_rarity=ERarity::Common;
 std::uint16_t m_stack_num=1; float m_timer=30,m_unable_picked_timer=0;
 bool removed=false,m_inertia_active=false;
 CGameWorld* GameWorld() const {return world;} int Id()const{return id;}
 bool IsMarkedForDes()const{return removed;} void MarkForDestroy(EEntityRemovalReason){removed=true;}
 void MergeNearbyDrops(); bool IsOwnedBy(const CPlayer&)const; bool CanBePickedUpBy(const CPlayer&)const;
};
${["void CDrop::MergeNearbyDrops(","bool CDrop::IsOwnedBy(","bool CDrop::CanBePickedUpBy("].map(m=>extract(drops,m)).join("\n")}
class CPetal;
struct Slot {std::vector<CPetal*> m_p_petals; std::vector<bool> m_bonus_active,m_reload_ignore_multiplier;
 float reload=0; void StartReload(int,float value){reload=value;} };
class CFlower : public CEntity {public: std::vector<Slot> slots; bool dirty=false;
 auto& GetSlots(){return slots;} void MarkFinalStatsDirty(){dirty=true;} };
struct Behavior {int clears=0; void OnPetalCleared(CPetal*,ERarity,CFlower*){++clears;} };
struct Prototype {std::unique_ptr<Behavior> m_p_behavior=std::make_unique<Behavior>();} prototype;
const Prototype* FindPetalPrototype(EPetalType){return &prototype;}
class CPetal : public CEntity {
 public: CEntity* owner=nullptr; bool m_clear_notified=false,m_reload_ignore_multiplier=false;
 float m_reload_override=-1; struct Stats{float reload=2;} m_final_petal_stats;
 ERarity m_rarity=ERarity::Common;
 CEntity* GetOwner()const{return owner;} EPetalType GetPetalType()const{return EPetalType::Basic;}
 void NotifyCleared(); void OnBeforeRemoved(EEntityRemovalReason);
};
${extract(petals,"void CPetal::NotifyCleared(")}
${extract(petals,"void CPetal::OnBeforeRemoved(")}
class CCorruptionState {public: CMobBase* m_p_owner=nullptr; int m_old_team=2; ~CCorruptionState();
 CMobBase* ResolveOwner()const{return m_p_owner;} };
${extract(states,"CCorruptionState::~CCorruptionState(")}

// Execute real SQL-generation/diff methods against a recording transaction
// transport, including failure+retry. No libmysql or DB is loaded.
namespace sqltest {
constexpr int kContainerInventory=0,kContainerSlots=1,kContainerSecondarySlots=2;
std::mutex g_publish_state_mutex; std::map<std::string,SPlayerAccount> g_published_accounts;
bool g_publish_state_seeded=true; std::vector<std::string> queries; std::string fail_on;
bool IsSandboxAccount(const std::string& name){return name.rfind("@sandbox:",0)==0;}
struct CMysqlConnection {bool Open(std::string*){return true;} std::string Quoted(const std::string& s){return "'"+s+"'";} };
bool EnsureSchema(CMysqlConnection&,std::string*){return true;}
bool Query(CMysqlConnection&,const std::string& s,std::string* error){queries.push_back(s);
 if(!fail_on.empty()&&s.find(fail_on)!=std::string::npos){if(error)*error="injected";return false;}return true;}
${extract(mysql,"void AppendItemRows(")}
${extract(mysql,"bool WriteAccountRows(")}
${extract(mysql,"bool PublishSnapshot(const SAccountSnapshot&")}
bool issued(const char* text){for(const auto& q:queries)if(q.find(text)!=std::string::npos)return true;return false;}
}
int main(){
 // New release before old press: both independent action and movement channels.
 CPlayerController ctl; CMobBase body; ClientOperate press,release;
 press.type=ClientOperate::Type::Input; press.move_x=127; press.is_attacking=true; press.is_defending=true;
 release=press; release.move_x=0; release.is_attacking=false; release.is_defending=false;
 check(ctl.PushOperate(press,8,100)&&ctl.PushOperate(release,1,101),"queue accepted");
 for(int i=0;i<8;i++)ctl.OnTick(&body,.05f);
 check(!body.attack&&!body.defend&&body.move==0,"stale future input cannot undo release");
 ctl.ResetOperate(); ctl.PushOperate(press,8,0xffffffffu);ctl.PushOperate(release,1,0);
 for(int i=0;i<8;i++)ctl.OnTick(&body,.05f);
 check(!body.attack&&!body.defend&&body.move==0,"sequence wrap works");
 ctl.ResetOperate();ctl.PushOperate(press,8,5);ctl.PushOperate(release);
 for(int i=0;i<8;i++)ctl.OnTick(&body,.05f);
 check(!body.attack&&!body.defend&&body.move==0,"legacy release fences scheduled input");
 // Stable contribution identity, even if connection id/address gets reused.
 CGameContext context; auto a=std::make_unique<CPlayer>();a->account="A";context.players.push_back(std::move(a));
 CDamageData damage{"A",20,120}; check(damage.ResolvePlayer(&context)!=nullptr,"online contributor");
 context.players.clear(); check(damage.ResolvePlayer(&context)==nullptr,"offline contributor safe");
 auto b=std::make_unique<CPlayer>();b->account="B";context.players.push_back(std::move(b));
 check(damage.ResolvePlayer(&context)==nullptr,"reused id does not inherit damage");
 auto again=std::make_unique<CPlayer>();again->account="A";again->id=9;auto* returning=again.get();context.players.push_back(std::move(again));
 check(damage.ResolvePlayer(&context)==returning,"same account reconnects");
 returning->progress="@sandbox:test";
 check(damage.ResolvePlayer(&context)==nullptr,"formal damage credit cannot enter a temporary progress scope");
 damage.m_progress_account=returning->progress;
 check(damage.ResolvePlayer(&context)==returning,"matching temporary damage credit resolves");
 returning->progress.clear();
 check(damage.ResolvePlayer(&context)==nullptr,"temporary damage credit cannot return to formal progress");
 // Private visibility/pickup and merging use accounts, never numeric ids.
 CGameWorld world;CDrop old,fresh;old.id=1;fresh.id=2;old.world=fresh.world=&world;
 old.m_owner_account=fresh.m_owner_account="A";fresh.m_owner_id=9;
 old.m_timer=.1f;fresh.m_timer=30;fresh.m_stack_num=5;fresh.m_unable_picked_timer=.2f;
 world.grid.entities={&old,&fresh};old.MergeNearbyDrops();check(old.m_stack_num==1,"pickup delay blocks merge");
 fresh.m_unable_picked_timer=0;fresh.m_inertia_active=true;old.MergeNearbyDrops();check(old.m_stack_num==1,"inertia blocks merge");
 fresh.m_inertia_active=false;old.MergeNearbyDrops();check(old.m_stack_num==6&&old.m_timer==30&&fresh.removed,"merged stack keeps later expiry");
 check(!old.CanBePickedUpBy(*context.players[0])&&old.CanBePickedUpBy(*returning),"drop owner remains A");
 old.m_owner_progress_account="@sandbox:test";returning->progress="@sandbox:test";
 check(old.CanBePickedUpBy(*returning),"temporary drop matches its progress scope");
 returning->progress.clear();check(!old.CanBePickedUpBy(*returning),"temporary drops cannot be claimed with formal progress");
 old.m_owner_progress_account.clear();
 old.m_owner_account.clear();check(!old.CanBePickedUpBy(*returning),"unknown legacy private owner fails closed");
 // Sleeping owner: cleanup detaches actual pointers and runs callback once.
 CFlower flower; CPetal petal;petal.owner=&flower;flower.slots.resize(1);
 flower.slots[0].m_p_petals={&petal};flower.slots[0].m_bonus_active={true};flower.slots[0].m_reload_ignore_multiplier={false};
 petal.OnBeforeRemoved(EEntityRemovalReason::Despawned);petal.OnBeforeRemoved(EEntityRemovalReason::Despawned);
 check(!flower.slots[0].m_p_petals[0]&&flower.dirty&&flower.slots[0].reload==2,"sleeping slot detached and reloaded");
 check(prototype.m_p_behavior->clears==1,"clear callback exactly once");
 {CCorruptionState state;state.m_p_owner=&body;body.Team()=0;}check(body.Team()==2,"ordinary corruption restores base team");
 {CCorruptionState state;state.m_p_owner=&body;body.Team()=5;}check(body.Team()==5,"new team not overwritten");
 // A single inventory mutation must not rewrite talents/codex. Failed writes
 // retain old published state so the same snapshot remains dirty on retry.
 using namespace sqltest;
 SPlayerAccount account;account.name="synthetic";account.password="not-a-real-hash";
 g_published_accounts[account.name]=account;account.inventory.push_back({1,1,5});
 SAccountSnapshot snap{std::make_shared<const SPlayerAccount>(account)};std::string error;
 fail_on="INSERT INTO account_items";
 check(!PublishSnapshot(snap,1,&error)&&issued("ROLLBACK"),"write failure rolls back");
 check(g_published_accounts[account.name].inventory.empty(),"failed publish does not advance cache");
 fail_on.clear();queries.clear();check(PublishSnapshot(snap,2,&error),"retry succeeds");
 check(issued("DELETE FROM account_items")&&!issued("DELETE FROM account_talents")&&!issued("DELETE FROM account_codex"),"only dirty inventory written");
 queries.clear();check(PublishSnapshot(snap,3,&error),"unchanged snapshot succeeds");
 check(!issued("INSERT INTO accounts")&&!issued("DELETE FROM account_items"),"unchanged account skipped");
 account.talents.push_back({static_cast<ETalentId>(1),ERarity::Common,1});account.codex.push_back({1,1,2});
 snap={std::make_shared<const SPlayerAccount>(account)};queries.clear();
 check(PublishSnapshot(snap,4,&error),"talent/codex changes publish");
 check(issued("DELETE FROM account_talents")&&issued("DELETE FROM account_codex")&&!issued("DELETE FROM account_items"),"talent/codex do not rewrite inventory");
 account.exp=42;snap={std::make_shared<const SPlayerAccount>(account)};queries.clear();
 check(PublishSnapshot(snap,5,&error)&&!issued("DELETE FROM account_"),"progress-only update leaves item/talent/codex tables alone");
 queries.clear();check(PublishSnapshot(SAccountSnapshot{},6,&error)&&issued("DELETE FROM accounts"),"full-snapshot removal kept");
 std::puts("PASS production input order/wrap/legacy, stable identities, drop timers, slot cleanup, teams, SQL diff/rollback/retry");
}
`, "utf8");
writeFileSync(path.join(directory, "CMakeLists.txt"), `cmake_minimum_required(VERSION 3.20)
project(ServerLifecycleChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(ServerLifecycleChecks checks.cpp)
target_compile_definitions(ServerLifecycleChecks PRIVATE NOMINMAX)
target_include_directories(ServerLifecycleChecks PRIVATE "${root.replaceAll("\\", "/")}/src")
enable_testing()
add_test(NAME lifecycle COMMAND ServerLifecycleChecks)
`);
for (const [command, args] of [
  ["cmake", ["-S",directory,"-B",path.join(directory,"build")]],
  ["cmake", ["--build",path.join(directory,"build"),"--config","Release","--parallel","2"]],
  ["ctest", ["--test-dir",path.join(directory,"build"),"-C","Release","-V"]],
]) {
  const result=spawnSync(command,args,{encoding:"utf8",timeout:60000,windowsHide:true});
  process.stdout.write(result.stdout||"");process.stderr.write(result.stderr||"");
  if(result.error)throw result.error;
  if(result.status!==0)process.exit(result.status||1);
}
// Cross-layer hooks/serialization use the same stable owner everywhere.
const reload=read("src/Server/HotReload/hot_reload_snapshot.cpp"),world=read("src/Server/Game/gameworld.cpp");
const transition=read("src/Server/Game/world_transition.cpp");
assert.match(reload,/Field\("account_name", record.m_account_name\)/);
assert.match(reload,/Field\("owner_account", drop->GetOwnerAccount\(\)\)/);
assert.match(reload,/RestoreOwnerAccount\(data.String\("owner_account"\)\)/);
assert.match(transition,/OnPlayerDepartureCommitted\(source, player, controlled\);[\s\S]*?ClearTeamOverrideStates\(\);[\s\S]*?OnPlayerArrivalCommitted\(destination, player\)/);
assert.match(transition,/before_source\.Restore\(\); before_destination\.Restore\(\)/);
assert.match(world,/entity->OnBeforeRemoved\(removal_reason\);[\s\S]*?m_p_entities\[id\].reset\(\)/);
assert.match(read("src/Server/Game/systems/snapshot_service.cpp"),/cached.drop_owner_account == player.GetAccountName\(\)/);
assert.doesNotMatch(reload,/record.m_player->/);
console.log("PASS ownership snapshot fields, visibility, transfer and destruction hooks");
