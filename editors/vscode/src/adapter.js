#!/usr/bin/env node

"use strict";

const childProcess = require("child_process");
const net = require("net");

let input = Buffer.alloc(0);
let backendInput = Buffer.alloc(0);
let backend = null;
let child = null;
let pending = [];
let internalInitializeSeq = 1000000000;

function encode(message) {
  const body = Buffer.from(JSON.stringify(message));
  return Buffer.concat([
    Buffer.from(`Content-Length: ${body.length}\r\n\r\n`),
    body,
  ]);
}

function writeFrontend(message) {
  process.stdout.write(encode(message));
}

function output(category, text) {
  if (!text) return;
  writeFrontend({
    seq: 0,
    type: "event",
    event: "output",
    body: { category, output: text },
  });
}

function parseFrames(buffer, onMessage) {
  for (;;) {
    const end = buffer.indexOf("\r\n\r\n");
    if (end < 0) return buffer;
    const header = buffer.subarray(0, end).toString();
    const match = /Content-Length:\s*(\d+)/i.exec(header);
    if (!match) throw new Error("Invalid DAP message header");
    const length = Number(match[1]);
    const start = end + 4;
    if (buffer.length < start + length) return buffer;
    onMessage(JSON.parse(buffer.subarray(start, start + length).toString()));
    buffer = buffer.subarray(start + length);
  }
}

function connect(port, request) {
  backend = net.createConnection({ host: "127.0.0.1", port }, () => {
    backend.write(
      encode({
        seq: internalInitializeSeq,
        type: "request",
        command: "initialize",
        arguments: { adapterID: "oak", linesStartAt1: true, columnsStartAt1: true },
      }),
    );
    pending.unshift(request);
  });
  backend.on("data", (chunk) => {
    backendInput = Buffer.concat([backendInput, chunk]);
    backendInput = parseFrames(backendInput, (message) => {
      if (
        message.type === "response" &&
        message.request_seq === internalInitializeSeq
      ) {
        for (const queued of pending.splice(0)) backend.write(encode(queued));
        return;
      }
      process.stdout.write(encode(message));
    });
  });
  backend.on("error", (error) => {
    output("stderr", `Oak debugger connection failed: ${error.message}\n`);
    process.exitCode = 1;
  });
  backend.on("close", () => {
    if (child && !child.killed) child.kill();
  });
}

function launch(request) {
  const args = request.arguments || {};
  const executable = args.oakExecutable || "oak";
  const childArgs = ["--debug", "--debug-port", "0", args.program].concat(
    args.args || [],
  );
  child = childProcess.spawn(executable, childArgs, {
    cwd: args.cwd || process.cwd(),
    stdio: ["ignore", "pipe", "pipe"],
  });
  child.stdout.on("data", (chunk) => output("stdout", chunk.toString()));
  let stderr = "";
  child.stderr.on("data", (chunk) => {
    stderr += chunk.toString();
    for (;;) {
      const end = stderr.indexOf("\n");
      if (end < 0) break;
      const line = stderr.slice(0, end + 1);
      stderr = stderr.slice(end + 1);
      const ready = /^OAK_DAP_PORT=(\d+)\s*$/.exec(line);
      if (ready) connect(Number(ready[1]), request);
      else output("stderr", line);
    }
  });
  child.on("error", (error) => {
    output("stderr", `Could not launch ${executable}: ${error.message}\n`);
  });
  child.on("close", (code) => {
    if (stderr) output("stderr", stderr);
    if (!backend) {
      writeFrontend({
        seq: 0,
        type: "event",
        event: "terminated",
        body: { restart: false },
      });
    }
    process.exitCode = code || 0;
  });
}

function handleFrontend(message) {
  if (backend) {
    backend.write(encode(message));
    return;
  }
  if (message.command === "initialize") {
    writeFrontend({
      seq: 0,
      type: "response",
      request_seq: message.seq,
      success: true,
      command: "initialize",
      body: {
        supportsConfigurationDoneRequest: true,
        supportsTerminateRequest: true,
        supportsEvaluateForHovers: true,
      },
    });
    return;
  }
  if (message.command === "launch") {
    launch(message);
    return;
  }
  if (message.command === "attach") {
    connect(Number(message.arguments.port), message);
    return;
  }
  pending.push(message);
}

process.stdin.on("data", (chunk) => {
  input = Buffer.concat([input, chunk]);
  input = parseFrames(input, handleFrontend);
});
process.stdin.on("end", () => {
  if (backend) backend.end();
  if (child && !child.killed) child.kill();
});
