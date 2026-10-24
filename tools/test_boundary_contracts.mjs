// Structural ratchets complement the executable lifecycle regressions.
// These reject known unsafe shortcuts when new gameplay features are added.
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = relative => readFileSync(path.join(root, relative), 'utf8');
function* cppFiles(directory) {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const target = path.join(directory, entry.name);
    if (entry.isDirectory()) yield* cppFiles(target);
    else if (entry.isFile() && entry.name.endsWith('.cpp')) yield target;
  }
}
const progressMethods = 'GetInventory|GetSlots|GetSecondarySlots|GetProgress|SetProgress|GetTalentPoints|SetTalentPoints|AddTalentPoints|GetTalents|SetTalents|GetCodex|AddCodexKill|SetSlot|ClearSlot|SetSecondarySlot|ClearSecondarySlot|TakeItem|AddItem|HasItem|CraftItem';
const unsafeProgress = new RegExp(`CAccountDataStore::(?:${progressMethods})\\(\\s*(?:player|target)(?:->|\\.)GetAccountName\\(`);
for (const file of cppFiles(path.join(root, 'src/Server')))
  assert.doesNotMatch(readFileSync(file, 'utf8'), unsafeProgress, `${file}: gameplay data must use progress scope, not auth identity`);

const player = read('src/Server/Game/player.h');
assert.doesNotMatch(read('src/Server/Game/player.cpp'),
  new RegExp(`CAccountDataStore::(?:${progressMethods})\\(\\s*m_account_name`));
assert.doesNotMatch(player, /SetSandboxAccountName/);
assert.match(player, /GetProgressAccountName/);
assert.match(player, /MatchesSession/);
assert.match(read('src/Server/Game/entities/mob.cpp'), /GetProgressAccountName\(\) == m_progress_account/);
assert.match(read('src/Server/Game/entities/drop.cpp'), /m_owner_progress_account == player\.GetProgressAccountName\(\)/);
const sandbox = read('src/Server/Game/gamecontrollers/sandbox_controller.cpp');
assert.doesNotMatch(sandbox, /m_original_accounts|m_admin_ids/);
assert.match(sandbox, /!IsAdmin\(player\)/);
assert.match(sandbox, /!IsMember\(world, \*player\)/);
assert.match(sandbox, /session_generation/);
const reports = read('src/Server/report.cpp');
assert.doesNotMatch(reports, /\.detach\(|g_async_report_results/);
assert.match(reports, /pending\.size\(\) >= 32/);
assert.match(reports, /MatchesSession\(job\.target\)/);
assert.match(reports, /worker\.join\(\)/);
const transfer = read('src/Server/Game/world_transition.cpp');
assert.equal((transfer.match(/source\.ForEachEntity\(/g) || []).length, 1, 'index structural ownership once per transfer');
assert.match(transfer, /DeferWorldTransfer/);
assert.match(transfer, /ActiveTickMarker\(\) = record\.active_marker/);
assert.match(read('src/Server/Game/gamecontrollers/mutable_terrain_controller.cpp'), /world\.Players\(\)/);
assert.match(read('src/Server/Game/wall_board.cpp'), /BroadcastTerrainWallRemove\(world,/);
const runtime = read('src/Server/Game/extensions/extension_runtime.cpp');
assert.match(runtime, /dynamic_cast<IMaintainedExtension\*>/);
assert.match(runtime, /RestoreScope\(m_globals/);
assert.doesNotMatch(runtime, /json_array_size\(globals\) != 1/);
console.log('PASS stable auth identity, scoped progress, session-aware permissions, bounded reports and generic extension contracts');
