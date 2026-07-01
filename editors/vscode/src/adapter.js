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
let startupRequest = null;
let startupResponded = false;
let terminated = false;
let backendTerminatedMessage = null;

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

function writeResponse(request, success, message) {
  writeFrontend({
    seq: 0,
    type: "response",
    request_seq: request.seq,
    success,
    command: request.command,
    message,
  });
}

function terminate() {
  if (terminated) return;
  terminated = true;
  writeFrontend({
    seq: 0,
    type: "event",
    event: "terminated",
    body: { restart: false },
  });
}

function startupFailed(request, message) {
  output("stderr", `${message}\n`);
  if (request && !startupResponded) {
    writeResponse(request, false, message);
    startupResponded = true;
  }
  terminate();
  process.exitCode = 1;
}

function childExitMessage(code, signal) {
  if (signal) return `Oak exited before the debugger was ready (signal ${signal})`;
  if (code === null || code === undefined)
    return "Oak exited before the debugger was ready";
  return `Oak exited before the debugger was ready (exit code ${code})`;
}

function adapterExitCode(code) {
  if (code === 0) return 0;
  return code > 0 && code <= 255 ? code : 1;
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
        startupRequest &&
        message.request_seq === startupRequest.seq &&
        (message.command === "launch" || message.command === "attach")
      ) {
        startupResponded = true;
      }
      if (
        message.type === "response" &&
        message.request_seq === internalInitializeSeq
      ) {
        for (const queued of pending.splice(0)) backend.write(encode(queued));
        return;
      }
      if (message.type === "event" && message.event === "terminated" && child) {
        backendTerminatedMessage = message;
        return;
      }
      process.stdout.write(encode(message));
    });
  });
  backend.on("error", (error) => {
    startupFailed(request, `Oak debugger connection failed: ${error.message}`);
  });
}

function launch(request) {
  const args = request.arguments || {};
  const executable = args.oakExecutable || "oak";
  startupRequest = request;
  startupResponded = false;
  if (typeof args.program !== "string" || args.program.length === 0) {
    startupFailed(request, "No Oak source file was provided to the debugger");
    return;
  }
  const debugPort =
    args.debugPort === undefined ? 4711 : Number(args.debugPort);
  if (
    !Number.isInteger(debugPort) ||
    debugPort < 0 ||
    debugPort > 65535
  ) {
    startupFailed(request, `Invalid Oak debug port: ${args.debugPort}`);
    return;
  }
  const childArgs = [
    "--debug",
    "--debug-port",
    String(debugPort),
    args.program,
  ].concat(args.args || []);
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
    startupFailed(request, `Could not launch ${executable}: ${error.message}`);
  });
  child.on("close", (code, signal) => {
    if (stderr) output("stderr", stderr);
    if (!backend) {
      if (!startupResponded)
        startupFailed(request, childExitMessage(code, signal));
      else
        terminate();
    } else if (backendTerminatedMessage) {
      process.stdout.write(encode(backendTerminatedMessage));
      backendTerminatedMessage = null;
      terminated = true;
    }
    process.exitCode = adapterExitCode(code);
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
    startupRequest = message;
    startupResponded = false;
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
  if (child && !child.killed && !terminated) child.kill();
});
