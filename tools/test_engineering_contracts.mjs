import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const read = file => readFileSync(new URL("../" + file, import.meta.url), "utf8");
const report = read("src/Server/report.cpp"), mail = read("src/Server/Auth/email_verification_service.cpp");
for (const [name, source] of [["reports", report], ["mail", mail]]) {
  assert.doesNotMatch(source, /std::system\s*\(|\b_?popen\s*\(|temp_directory_path|ShellQuote/, name);
}
assert.match(report, /cancellation\.request_stop\(\)/);
assert.match(report, /job\.diagnostic/);
assert.match(mail, /remote_transport::SendMail/);
assert.match(mail, /m_cancellation\.request_stop\(\)/);
assert.match(read("src/Server/main.cpp"), /if \(server\.Init\(\)\) server\.Run\(\)/);
const cmake = read("CMakeLists.txt"), workflow = read(".github/workflows/build-server.yml");
const buildLine = workflow.split("\n").find(line => line.includes("cmake --build") && line.includes("--target"));
assert.ok(buildLine);
const builtTargets = new Set(buildLine.split("--target")[1].trim().split(/\s+/));
for (const match of cmake.matchAll(/add_test\(NAME \S+ COMMAND (FlorrBt_\w+)/g)) {
  assert.ok(builtTargets.has(match[1]), `CTest executable ${match[1]} must be built on a clean CI runner`);
}
assert.match(workflow, /libcurl\.dll/);
assert.match(workflow, /curl-COPYING\.txt/);
const packageScript = read("deploy/windows/package_zip.ps1");
assert.match(packageScript, /Copy-File .*"libcurl\.dll"/);
assert.match(packageScript, /Copy-File .*"curl-COPYING\.txt"/);
assert.match(read("cmake/RemoteTransportDependency.cmake"), /URL_HASH SHA256=[a-f0-9]{64}/);
console.log("PASS shell-free workers, cancellation, explicit startup result, CI test targets and packaged transport");
