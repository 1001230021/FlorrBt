// Connects to the game server as a raw TCP client, authenticates using the
// web client's own protocol code, and prints the first few frames received,
// especially the Environment message bytes.
const net = require("net");

async function main() {
  const proto = await import(
    new URL("../web/src/protocol.js", "file:///" + __dirname.replace(/\\/g, "/") + "/").href
  );

  const account = { name: "1001230021", password: "" };
  const authBytes = proto.packAuth({
    mode: proto.AuthMode.Login,
    name: account.name,
    password: account.password,
  });

  const socket = net.connect(10012, "127.0.0.1");
  let buffer = Buffer.alloc(0);
  let frameCount = 0;

  socket.on("connect", () => {
    console.log("connected, sending auth", authBytes.length, "bytes");
    socket.write(Buffer.from(authBytes));
  });

  socket.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      if (buffer.length < 2) return;
      const len = buffer.readUInt16LE(0);
      if (buffer.length < 2 + len) return;
      const payload = buffer.subarray(2, 2 + len);
      buffer = buffer.subarray(2 + len);
      frameCount += 1;
      const type = payload[0];
      console.log(
        `frame #${frameCount} type=0x${type.toString(16)} len=${len}`,
        type === 0x14 ? ` env bytes: ${[...payload].map((b) => b.toString(16)).join(" ")}` : "",
      );
      if (frameCount >= 8) {
        socket.destroy();
        process.exit(0);
      }
    }
  });

  socket.on("error", (e) => {
    console.log("socket error:", e.message);
    process.exit(1);
  });
  setTimeout(() => {
    console.log("timeout");
    process.exit(2);
  }, 15000);
}

main().catch((e) => {
  console.log("fatal:", e.message);
  process.exit(1);
});
