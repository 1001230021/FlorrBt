// Check the Git file list, not the working directory: local runtime data may
// legitimately exist next to a checkout but must never enter a source commit.
import { execFileSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";

const paths = execFileSync("git", ["ls-files", "-z"], { encoding: "utf8" }).split("\0").filter(Boolean);
const forbidden = /(?:^|\/)(?:node_modules|artifacts|build|\.vs|__pycache__)(?:\/|$)|\.(?:exe|dll|lib|obj|pdb|log|pyc|dmp|suo|user)$|^data\/(?:server\.cfg$|accounts\.json(?:$|\.(?!example$))|unique_petals\.json$|hot_reload\/)|^deploy\/windows-web\/web\.env\.ps1$|(?:^|\/)\.env(?:$|\.(?!example$))|\.local\.props$/i;
const privateKey = /-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----/;
const credential = /(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|sk-[A-Za-z0-9_-]{24,}|AKIA[A-Z0-9]{16}|xox[baprs]-[A-Za-z0-9-]{20,})/;
const failures = [];
for (const path of paths) {
  if (forbidden.test(path)) failures.push(`${path}: runtime/local/build file is tracked`);
  if (!existsSync(path) || /\.(?:ttf|woff2?|png|jpe?g|gif|webp|mp3|ogg)$/i.test(path)) continue;
  const text = readFileSync(path, "utf8");
  if (privateKey.test(text) || credential.test(text)) failures.push(`${path}: possible credential (value withheld)`);
  if (/\.vcxproj$/i.test(path) && /[A-Z]:\\(?:Users|VSProject|库)\\/i.test(text))
    failures.push(`${path}: machine-specific SDK path; use dependencies.props/local settings`);
}
if (failures.length) {
  console.error(failures.join("\n")); process.exitCode = 1;
} else console.log(`PASS source hygiene (${paths.length} tracked files; no local runtime/configuration/build files or recognized credentials)`);
