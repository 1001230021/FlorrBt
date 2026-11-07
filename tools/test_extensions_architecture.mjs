// Structural ratchet: old feature ownership must not creep back into the
// controller/server/network after the extension migration.
import assert from 'node:assert/strict';
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = file => readFileSync(path.join(root, file), 'utf8');
const server = read('src/Server/server.h');
const network = read('src/Server/Module/network_module.cpp');
const controller = read('src/Server/Game/gamecontrollers/opencontroller.h');
const report = read('src/Server/report.cpp');
assert.match(server, /unique_ptr<CExtensionRuntime>/);
assert.doesNotMatch(server, /struct SChatEntry|m_chats|SubmitChat|GetWeatherDiurnalSystem/);
assert.doesNotMatch(controller, /m_squad_|GetSquadPlayerList|TryInviteSquad|SpawnEnvironment/);
assert.doesNotMatch(network, /CWeatherExtension|dynamic_cast<COpenController|INetworkModule::BroadcastChat/);
assert.doesNotMatch(report, /dynamic_cast<COpenController|m_squad_|BuildSquadMetadata|TryInviteSquad/);
assert.match(read('src/Server/Game/systems/snapshot_service.cpp'), /Find<ISquadView>/);
assert.match(read('src/Server/Game/extensions/squad_extension.h'), /Members\(const CPlayer&\) const/);
assert.match(read('src/Server/Game/extensions/extension_runtime.h'), /vector<std::unique_ptr<CExtension>>/);
// 玩法只依赖能力接口，账本只返回事务结果。
for (const file of readdirSync(path.join(root, 'src/Server/Game'), { recursive: true })) {
  if (!/\.(cpp|h)$/.test(file)) continue;
  const source = read(path.join('src/Server/Game', file));
  assert.doesNotMatch(source, /CServer::GetInstance|#include\s+"[^"\n]*(?:network_module|server)\.h"/, file);
}
assert.doesNotMatch(read('src/Server/Persistence/unique_petal_registry.cpp'), /Game\/|Module\/|CServer|QueueInventory/);
assert.doesNotMatch(read('src/Server/Game/extensions/party_extension.cpp'), /dynamic_cast<IPartyDelivery/);
for (const old of [
  'src/Server/Game/gamecontrollers/weather_diurnal_open_controller.h',
  'src/Server/Game/gamecontrollers/weather_diurnal_open_controller.cpp',
  'src/Server/Game/systems/weather_diurnal_system.h',
  'src/Server/Game/systems/weather_diurnal_system.cpp',
]) assert.equal(existsSync(path.join(root, old)), false, `${old} must be removed`);
for (const project of ['FlorrBt.Server.vcxproj', 'FlorrBt.Server.vcxproj.filters']) {
  const xml = read(project);
  assert.doesNotMatch(xml, /weather_diurnal_open_controller|weather_diurnal_system/);
  for (const [, file] of xml.matchAll(/<(?:ClCompile|ClInclude) Include="([^"]+)"/g))
    assert.ok(existsSync(path.join(root, file.replaceAll('\\', '/'))), `${project}: missing ${file}`);
}
console.log('PASS extension ownership, narrow capability consumers, const squad queries and removed legacy sources');
