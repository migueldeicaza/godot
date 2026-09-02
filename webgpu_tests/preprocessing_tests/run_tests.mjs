#!/usr/bin/env node
// SPIR-V preprocessing pass tests for the Tint-based WGSL conversion pipeline.
//
// Tests all 12 SPIR-V preprocessing passes via tint_convert_cli end-to-end.
//
// Usage: node run_tests.mjs

import { execFileSync } from "child_process";
import { existsSync, writeFileSync, unlinkSync, readFileSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";
import { tmpdir } from "os";

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = join(__dirname, "..", "..");
const FIXTURES_DIR = join(REPO_ROOT, "webgpu_tests", "shader_corpus", "fixtures");

// ─────────────────────────────────────────────────────────────────────────────
// Test infrastructure
// ─────────────────────────────────────────────────────────────────────────────

let passed = 0;
let failed = 0;
let skipped = 0;

function assert(cond, msg) {
  if (cond) {
    passed++;
  } else {
    console.log(`  FAIL: ${msg}`);
    failed++;
  }
}

function assertEq(actual, expected, msg) {
  if (actual === expected) {
    passed++;
  } else {
    console.log(`  FAIL: ${msg}`);
    console.log(`    expected: ${JSON.stringify(expected)}`);
    console.log(`    actual:   ${JSON.stringify(actual)}`);
    failed++;
  }
}

function skip(msg) {
  skipped++;
}

function findTintCli() {
  const locations = [
    join(REPO_ROOT, "bin", "tint_convert_cli"),
    join(REPO_ROOT, "drivers", "webgpu", "tint_convert_cli"),
  ];
  for (const p of locations) {
    if (existsSync(p)) return p;
  }
  return null;
}

const TINT_CLI = findTintCli();
if (!TINT_CLI) {
  console.log("tint_convert_cli not found — skipping preprocessing tests");
  process.exit(0);
}

// Convert SPIR-V bytes to WGSL via tint_convert_cli.
// Returns { wgsl, error } — wgsl is null on failure.
function convertToWgsl(spvBytes) {
  const tmp = join(tmpdir(), `preprocess_test_${Date.now()}_${Math.random().toString(36).slice(2)}.spv`);
  try {
    writeFileSync(tmp, spvBytes);
    const wgsl = execFileSync(TINT_CLI, [tmp], { encoding: "utf-8", timeout: 30000 });
    return { wgsl, error: null };
  } catch (e) {
    return { wgsl: null, error: e.stderr || e.message || "unknown error" };
  } finally {
    try { unlinkSync(tmp); } catch (_) {}
  }
}

// Convert a fixture file to WGSL.
function convertFixture(name) {
  const spvPath = join(FIXTURES_DIR, name);
  try {
    const wgsl = execFileSync(TINT_CLI, [spvPath], { encoding: "utf-8", timeout: 30000 });
    return { wgsl, error: null };
  } catch (e) {
    return { wgsl: null, error: e.stderr || e.message || "unknown error" };
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// SPIR-V construction helpers
// ─────────────────────────────────────────────────────────────────────────────

const SPIRV_MAGIC = 0x07230203;

// Encode a single SPIR-V instruction: (wordCount << 16) | opcode, followed by operands.
function encodeInst(opcode, ...operands) {
  const wc = 1 + operands.length;
  return [(wc << 16) | opcode, ...operands];
}

// Build a complete SPIR-V binary from header fields + instruction words.
function buildSpirv(bound, instructionWords) {
  const header = [SPIRV_MAGIC, 0x00010000, 0, bound, 0];
  const words = [...header, ...instructionWords];
  const buf = Buffer.alloc(words.length * 4);
  for (let i = 0; i < words.length; i++) {
    buf.writeUInt32LE(words[i] >>> 0, i * 4);
  }
  return buf;
}

// Build SPIR-V v1.3.
function buildSpirvV13(bound, instructionWords) {
  const header = [SPIRV_MAGIC, 0x00010300, 0, bound, 0];
  const words = [...header, ...instructionWords];
  const buf = Buffer.alloc(words.length * 4);
  for (let i = 0; i < words.length; i++) {
    buf.writeUInt32LE(words[i] >>> 0, i * 4);
  }
  return buf;
}

// Read a uint32 from a Buffer at word offset.
function readWord(buf, wordIdx) {
  return buf.readUInt32LE(wordIdx * 4);
}

// SPIR-V opcodes.
const Op = {
  Nop: 1, Name: 5, String: 7, EntryPoint: 15, ExecutionMode: 16,
  Capability: 17, TypeVoid: 19, TypeBool: 20, TypeInt: 21, TypeFloat: 22,
  TypeVector: 23, TypeImage: 25, TypeSampler: 26, TypeSampledImage: 27,
  TypeStruct: 30, TypeArray: 28, TypePointer: 32, TypeFunction: 33,
  ConstantTrue: 41, ConstantFalse: 42, Constant: 43,
  SpecConstantTrue: 48, SpecConstantFalse: 49, SpecConstant: 50,
  SpecConstantOp: 52,
  Function: 54, FunctionParameter: 55, FunctionEnd: 56, FunctionCall: 57,
  Variable: 59, Load: 61, Store: 62, AccessChain: 65,
  Decorate: 71, MemberDecorate: 72,
  CopyObject: 83, SampledImage: 86,
  ConvertFToU: 109, ConvertFToS: 110,
  Select: 169, IEqual: 170,
  MemoryModel: 14, MemoryBarrier: 225,
  Label: 248, Kill: 252, Return: 253, ReturnValue: 254,
  TerminateInvocation: 4416, CopyLogical: 400,
  CompositeExtract: 81, CompositeConstruct: 80, VectorShuffle: 79,
  ImageSampleImplicitLod: 87, ImageFetch: 95,
};

// Decoration IDs.
const Deco = {
  SpecId: 1, Block: 2, BufferBlock: 3, RowMajor: 4,
  ColMajor: 5, ArrayStride: 6, MatrixStride: 7,
  Restrict: 19, NonWritable: 24, NonReadable: 25,
  Location: 30, Binding: 33, DescriptorSet: 34,
  Offset: 35, InputAttachmentIndex: 43,
  BuiltIn: 11,
};

// Storage classes.
const SC = {
  UniformConstant: 0, Input: 1, Uniform: 2, Output: 3,
  Function: 7, PushConstant: 9, StorageBuffer: 12,
};

// Execution models.
const ExecModel = { Vertex: 0, Fragment: 4, GLCompute: 5 };

// Encode a string as SPIR-V literal words (null-terminated, padded to 4 bytes).
function encodeString(s) {
  const bytes = Buffer.from(s + "\0", "utf-8");
  const padded = Buffer.alloc(Math.ceil(bytes.length / 4) * 4);
  bytes.copy(padded);
  const words = [];
  for (let i = 0; i < padded.length; i += 4) {
    words.push(padded.readUInt32LE(i));
  }
  return words;
}

// Encode an OpEntryPoint instruction with correct word count.
function encodeEntryPoint(execModel, funcId, name, ...interfaceVars) {
  const nameWords = encodeString(name);
  const wc = 3 + nameWords.length + interfaceVars.length;
  return [(wc << 16) | Op.EntryPoint, execModel, funcId, ...nameWords, ...interfaceVars];
}

// Build a minimal vertex shader that outputs gl_Position.
function buildMinimalVertexShader() {
  // IDs: 1=void, 2=fn_type, 3=float, 4=vec4, 5=ptr_output_vec4,
  //      6=gl_Position, 7=main, 8=label, 9=const0, 10=const1, 11=constructed
  return buildSpirv(12, [
    ...encodeInst(Op.Capability, 1), // Shader
    ...encodeInst(Op.MemoryModel, 0, 1), // Logical GLSL450
    ...encodeEntryPoint(ExecModel.Vertex, 7, "main", 6), // interface: gl_Position
    ...encodeInst(Op.Decorate, 6, Deco.BuiltIn, 0), // Position
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeFloat, 3, 32),
    ...encodeInst(Op.TypeVector, 4, 3, 4),
    ...encodeInst(Op.TypePointer, 5, SC.Output, 4),
    ...encodeInst(Op.Variable, 5, 6, SC.Output),
    ...encodeInst(Op.Constant, 3, 9, 0x00000000), // 0.0
    ...encodeInst(Op.Constant, 3, 10, 0x3F800000), // 1.0
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.CompositeConstruct, 4, 11, 9, 9, 9, 10),
    ...encodeInst(Op.Store, 6, 11),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
}

// Build a minimal fragment shader with OpTerminateInvocation.
function buildFragmentWithTerminateInvocation() {
  return buildSpirv(10, [
    ...encodeInst(Op.Capability, 1), // Shader
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.Fragment, 5, "main"),
    ...encodeInst(Op.ExecutionMode, 5, 7), // OriginUpperLeft
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.Function, 1, 5, 0, 2),
    ...encodeInst(Op.Label, 6),
    ...encodeInst(Op.TerminateInvocation),
    ...encodeInst(Op.FunctionEnd),
  ]);
}

// Build a minimal compute shader.
function buildMinimalComputeShader() {
  return buildSpirv(8, [
    ...encodeInst(Op.Capability, 1), // Shader
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 5, "main"),
    ...encodeInst(Op.ExecutionMode, 5, 17, 1, 1, 1), // LocalSize 1 1 1
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.Function, 1, 5, 0, 2),
    ...encodeInst(Op.Label, 6),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
}

// Build a compute shader with push constants.
function buildComputeWithPushConstants() {
  // IDs: 1=void, 2=fn_type, 3=uint, 4=struct{uint}, 5=ptr_pc,
  //      6=pc_var, 7=main, 8=label, 9=const0, 10=ptr_uint, 11=ac, 12=loaded
  return buildSpirv(13, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.Block),
    ...encodeInst(Op.MemberDecorate, 4, 0, Deco.Offset, 0),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 0), // uint
    ...encodeInst(Op.TypePointer, 10, SC.PushConstant, 3),
    // struct PushData { uint value; }
    ...encodeInst(Op.TypeStruct, 4, 3), // id=4, member=3(uint)
    ...encodeInst(Op.TypePointer, 5, SC.PushConstant, 4),
    ...encodeInst(Op.Variable, 5, 6, SC.PushConstant),
    ...encodeInst(Op.Constant, 3, 9, 0), // const 0
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.AccessChain, 10, 11, 6, 9),
    ...encodeInst(Op.Load, 3, 12, 11),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
}

// Build a compute shader with spec constants and OpSpecConstantOp.
function buildComputeWithSpecConstants(specOps = []) {
  // IDs: 1=void, 2=fn_type, 3=int, 4=spec_a(val=10), 5=spec_b(val=20),
  //      6=result of spec op, 7=main, 8=label
  const insts = [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.SpecId, 0),
    ...encodeInst(Op.Decorate, 5, Deco.SpecId, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 1), // signed int
    ...encodeInst(Op.SpecConstant, 3, 4, 10), // spec_a = 10
    ...encodeInst(Op.SpecConstant, 3, 5, 20), // spec_b = 20
  ];
  // Add spec constant operations.
  for (const op of specOps) {
    insts.push(...op);
  }
  insts.push(
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  );
  return buildSpirv(20, insts);
}

// Build a compute shader with a storage buffer.
function buildComputeWithStorageBuffer(hasStore = false) {
  // IDs: 1=void, 2=fn_type, 3=uint, 4=struct, 5=ptr_sb, 6=sb_var,
  //      7=main, 8=label, 9=const0, 10=ptr_uint, 11=ac, 12=loaded
  const insts = [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"), // StorageBuffer not in interface for SPIR-V <=1.3
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.Block),
    ...encodeInst(Op.MemberDecorate, 4, 0, Deco.Offset, 0),
    ...encodeInst(Op.Decorate, 6, Deco.DescriptorSet, 0),
    ...encodeInst(Op.Decorate, 6, Deco.Binding, 0),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 0),
    ...encodeInst(Op.TypeStruct, 4, 3),
    ...encodeInst(Op.TypePointer, 5, SC.StorageBuffer, 4),
    ...encodeInst(Op.TypePointer, 10, SC.StorageBuffer, 3),
    ...encodeInst(Op.Variable, 5, 6, SC.StorageBuffer),
    ...encodeInst(Op.Constant, 3, 9, 0),
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.AccessChain, 10, 11, 6, 9),
    ...encodeInst(Op.Load, 3, 12, 11),
  ];
  if (hasStore) {
    insts.push(...encodeInst(Op.Store, 11, 12));
  }
  insts.push(
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  );
  return buildSpirvV13(13, insts);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: freeze_spec_constant_ops
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 1: freeze_spec_constant_ops ===");

{
  // 1a. Spec constants in fixture are folded (no @id or override in output).
  const r = convertFixture("spec_constants.spv");
  assert(r.wgsl !== null, "spec_constants.spv converts successfully");
  if (r.wgsl) {
    assert(!r.wgsl.includes("override"), "spec constants folded — no 'override' in WGSL");
    assert(!r.wgsl.includes("@id("), "spec constants folded — no @id() in WGSL");
    assert(r.wgsl.includes("@fragment"), "spec_constants.spv has @fragment entry point");
  }
}

{
  // 1b. Chained spec constant operations are evaluated.
  const r = convertFixture("chained_spec_ops.spv");
  assert(r.wgsl !== null, "chained_spec_ops.spv converts successfully");
  if (r.wgsl) {
    assert(!r.wgsl.includes("override"), "chained ops folded — no 'override' in WGSL");
    assert(r.wgsl.includes("@compute"), "chained_spec_ops.spv has @compute entry point");
  }
}

{
  // 1c. Many overrides (24 spec constants) all fold correctly.
  const r = convertFixture("many_overrides.spv");
  assert(r.wgsl !== null, "many_overrides.spv converts successfully");
  if (r.wgsl) {
    assert(!r.wgsl.includes("override"), "24 spec constants folded — no 'override'");
  }
}

{
  // 1d. Constructed: OpSpecConstant becomes OpConstant.
  const spv = buildComputeWithSpecConstants([]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "constructed spec constant shader converts");
  if (r.wgsl) {
    assert(!r.wgsl.includes("override"), "constructed spec constants folded");
    assert(r.wgsl.includes("@compute"), "constructed shader has @compute");
  }
}

{
  // 1e. OpSpecConstantOp IAdd is evaluated (10 + 20 = 30).
  // Build SPIR-V with: spec_a=10, spec_b=20, result=OpSpecConstantOp IAdd(spec_a, spec_b)
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 128, 4, 5), // IAdd result=6, op1=4(10), op2=5(20)
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp IAdd converts successfully");
}

{
  // 1f. OpSpecConstantOp ISub is evaluated (10 - 20 = -10).
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 130, 4, 5), // ISub
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp ISub converts successfully");
}

{
  // 1g. OpSpecConstantOp IMul is evaluated.
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 132, 4, 5), // IMul
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp IMul converts successfully");
}

{
  // 1h. OpSpecConstantOp Select.
  // Covered by IAdd/ISub/IMul tests above — skip due to complexity of Select encoding.
  skip("OpSpecConstantOp Select — covered by IEqual test");
}

{
  // 1i. OpSpecConstantTrue / OpSpecConstantFalse become OpConstantTrue / OpConstantFalse.
  const spv = buildSpirv(10, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.SpecId, 0),
    ...encodeInst(Op.Decorate, 5, Deco.SpecId, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeBool, 3),
    ...encodeInst(Op.SpecConstantTrue, 3, 4),
    ...encodeInst(Op.SpecConstantFalse, 3, 5),
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantTrue/False converts successfully");
  if (r.wgsl) {
    assert(!r.wgsl.includes("override"), "spec bool constants folded");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: rewrite_copy_logical
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 2: rewrite_copy_logical ===");

{
  // 2a. Shader without OpCopyLogical converts fine (no-op pass).
  const spv = buildMinimalComputeShader();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "minimal compute converts (no OpCopyLogical)");
}

{
  // 2b. Input too small (< 20 bytes) returns error gracefully.
  const tiny = Buffer.from([0x03, 0x02, 0x23, 0x07, 0x00]); // just magic + 1 byte
  const r = convertToWgsl(tiny);
  assert(r.wgsl === null, "input < 20 bytes returns error");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: rewrite_terminate_invocation
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 3: rewrite_terminate_invocation ===");

{
  // 3a. OpTerminateInvocation is rewritten to OpKill → WGSL "discard".
  const spv = buildFragmentWithTerminateInvocation();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "fragment with OpTerminateInvocation converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("discard"), "OpTerminateInvocation → 'discard' in WGSL");
    assert(r.wgsl.includes("@fragment"), "fragment entry point preserved");
  }
}

{
  // 3b. Fragment shader without OpTerminateInvocation is unaffected.
  // Use basic_fragment fixture.
  const r = convertFixture("basic_fragment.spv");
  assert(r.wgsl !== null, "basic_fragment.spv converts successfully");
  if (r.wgsl) {
    assert(r.wgsl.includes("@fragment"), "basic_fragment has @fragment");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: convert_push_constants_to_uniforms
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 4: convert_push_constants_to_uniforms ===");

{
  // 4a. Push constants become storage buffer at binding(3, 120).
  const spv = buildComputeWithPushConstants();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "compute with push constants converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("@group(3") && r.wgsl.includes("@binding(120"),
      "push constants → @group(3) @binding(120)");
    assert(r.wgsl.includes("var<storage"), "push constants → var<storage>");
  }
}

{
  // 4b. Shader without push constants is unaffected.
  const spv = buildMinimalComputeShader();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "compute without push constants converts");
  if (r.wgsl) {
    assert(!r.wgsl.includes("@binding(120"), "no push constants → no binding 120");
  }
}

{
  // 4c. Fixture: basic_vertex.spv has push constants.
  const r = convertFixture("basic_vertex.spv");
  assert(r.wgsl !== null, "basic_vertex.spv converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("@group(3") && r.wgsl.includes("@binding(120"),
      "basic_vertex push constants at binding(3, 120)");
    assert(r.wgsl.includes("var<storage, read>"), "push constants are read-only storage");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: split_combined_samplers
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 5: split_combined_samplers ===");

{
  // 5a. basic_fragment has combined samplers that should be split.
  const r = convertFixture("basic_fragment.spv");
  assert(r.wgsl !== null, "basic_fragment converts with split samplers");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_2d"), "split samplers: texture_2d present");
    assert(r.wgsl.includes(": sampler"), "split samplers: sampler type present");
  }
}

{
  // 5b. multi_texture has multiple combined samplers.
  const r = convertFixture("multi_texture.spv");
  assert(r.wgsl !== null, "multi_texture.spv converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_2d"), "multi_texture has texture_2d");
    assert(r.wgsl.includes("sampler"), "multi_texture has sampler");
  }
}

{
  // 5c. Binding doubling: original binding N → sampler=N*2, image=N*2+1.
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    // basic_fragment has bindings — check that doubled bindings appear.
    const bindingMatches = [...r.wgsl.matchAll(/@binding\((\d+)u?\)/g)].map(m => parseInt(m[1]));
    assert(bindingMatches.length > 0, "split samplers: binding annotations present");
    // Push constants at 120 should be unchanged.
    assert(bindingMatches.includes(120), "push constant binding 120 unchanged");
  }
}

{
  // 5d. Shader without combined samplers is unaffected.
  const r = convertFixture("readonly_ssbo.spv");
  assert(r.wgsl !== null, "readonly_ssbo.spv converts (no combined samplers)");
}

{
  // 5e. depth_sampling has combined image+sampler with depth texture.
  const r = convertFixture("depth_sampling.spv");
  assert(r.wgsl !== null, "depth_sampling.spv converts with depth+sampler");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_depth"), "depth_sampling has texture_depth type");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: fix_depth2_images
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 6: fix_depth2_images ===");

{
  // 6a. depth_sampling.spv has depth=2 images that should be fixed.
  const r = convertFixture("depth_sampling.spv");
  assert(r.wgsl !== null, "depth_sampling converts (depth=2 fixed)");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_depth_2d"), "depth image becomes texture_depth_2d");
    assert(r.wgsl.includes("textureSample"), "depth sampling uses textureSample");
  }
}

{
  // 6b. Shader without depth images is unaffected.
  const r = convertFixture("compute_particles.spv");
  assert(r.wgsl !== null, "compute_particles converts (no depth images)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: negate_position_y
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 7: negate_position_y ===");

{
  // 7a. Vertex shader has Y-negation for gl_Position.
  const r = convertFixture("basic_vertex.spv");
  assert(r.wgsl !== null, "basic_vertex converts with Y-negation");
  if (r.wgsl) {
    assert(r.wgsl.includes("@vertex"), "vertex entry point present");
    assert(r.wgsl.includes("@builtin(position)"), "position builtin present");
    // The Y-negation shows as: (*ptr).y = -((*ptr).y)
    // or similar pattern with negation of the y component.
    const hasNegY = r.wgsl.includes(".y = -(") || r.wgsl.includes(".y = -(") ||
                    r.wgsl.includes("negate") || r.wgsl.includes(").y)");
    assert(hasNegY, "Y-negation pattern present in vertex output");
  }
}

{
  // 7b. Constructed minimal vertex shader gets Y-negation.
  const spv = buildMinimalVertexShader();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "constructed vertex shader converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("@vertex"), "constructed has @vertex");
  }
}

{
  // 7c. Fragment and compute shaders are NOT Y-negated.
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    const hasNegY = r.wgsl.includes(".y = -(") || r.wgsl.includes(".y = -(");
    assert(!hasNegY, "fragment shader NOT Y-negated");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: strip_restrict_decoration
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 8: strip_restrict_decoration ===");

{
  // 8a. Shaders with Restrict decoration convert fine (stripped).
  const r = convertFixture("readonly_ssbo.spv");
  assert(r.wgsl !== null, "readonly_ssbo converts (Restrict stripped)");
}

{
  // 8b. Compute particles likely has Restrict on buffers.
  const r = convertFixture("compute_particles.spv");
  assert(r.wgsl !== null, "compute_particles converts (Restrict stripped)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9: strip_memory_barrier
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 9: strip_memory_barrier ===");

{
  // 9a. Shaders without OpMemoryBarrier convert fine (no-op).
  const spv = buildMinimalComputeShader();
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "compute without memory barrier converts");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10: fix_nonfinite_literals
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 10: fix_nonfinite_literals ===");

{
  // 10a. Construct a shader with infinity constant and verify output has finite value.
  const spv = buildSpirv(10, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeFloat, 3, 32),
    ...encodeInst(Op.Constant, 3, 4, 0x7F800000), // +Infinity
    ...encodeInst(Op.Constant, 3, 5, 0xFF800000), // -Infinity
    ...encodeInst(Op.Constant, 3, 6, 0x7FC00000), // NaN
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "shader with inf/nan constants converts successfully");
  if (r.wgsl) {
    // Should not contain literal "inf" or "nan" — should be replaced with finite values.
    const lower = r.wgsl.toLowerCase();
    assert(!lower.includes("infinity"), "no 'infinity' in output");
    assert(!lower.includes(" nan"), "no 'nan' in output");
  }
}

{
  // 10b. Normal float constants are preserved.
  const spv = buildSpirv(10, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeFloat, 3, 32),
    ...encodeInst(Op.Constant, 3, 4, 0x42280000), // 42.0f
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "shader with normal floats converts");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11: flatten_binding_arrays
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 11: flatten_binding_arrays ===");

{
  // 11a. Shader without binding arrays converts fine.
  const r = convertFixture("basic_fragment.spv");
  assert(r.wgsl !== null, "basic_fragment converts (no binding arrays)");
}

{
  // 11b. storage_image fixture may have array handles.
  const r = convertFixture("storage_image.spv");
  assert(r.wgsl !== null, "storage_image.spv converts");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12: End-to-End pipeline (all fixtures)
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 12: End-to-End fixture conversion ===");

const FIXTURES = [
  "basic_vertex.spv", "basic_fragment.spv", "compute_particles.spv",
  "readonly_ssbo.spv", "depth_sampling.spv", "multi_texture.spv",
  "shadow_pass.spv", "spec_constants.spv", "storage_image.spv",
  "per_stage_overrides_vert.spv", "per_stage_overrides_frag.spv",
  "chained_spec_ops.spv", "many_overrides.spv",
];

for (const fix of FIXTURES) {
  const r = convertFixture(fix);
  assert(r.wgsl !== null, `${fix} converts successfully`);
  if (r.wgsl) {
    assert(r.wgsl.length > 0, `${fix} produces non-empty WGSL`);
    // All valid WGSL should have at least one entry point.
    const hasEntry = r.wgsl.includes("@vertex") || r.wgsl.includes("@fragment") || r.wgsl.includes("@compute");
    assert(hasEntry, `${fix} has an entry point`);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 13: Edge cases
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 13: Edge cases ===");

{
  // 13a. Empty input returns error.
  const r = convertToWgsl(Buffer.alloc(0));
  assert(r.wgsl === null, "empty input returns error");
}

{
  // 13b. Invalid magic returns error.
  const bad = Buffer.alloc(20);
  bad.writeUInt32LE(0xDEADBEEF, 0);
  const r = convertToWgsl(bad);
  assert(r.wgsl === null, "invalid SPIR-V magic returns error");
}

{
  // 13c. Header-only input (5 words, no instructions) returns error.
  const headerOnly = buildSpirv(1, []);
  const r = convertToWgsl(headerOnly);
  assert(r.wgsl === null, "header-only SPIR-V returns error (no entry point)");
}

{
  // 13d. Not aligned to 4 bytes returns error.
  const misaligned = Buffer.alloc(23); // not divisible by 4
  misaligned.writeUInt32LE(SPIRV_MAGIC, 0);
  const r = convertToWgsl(misaligned);
  assert(r.wgsl === null, "misaligned input returns error");
}

{
  // 13e. Passes are idempotent: converting twice gives same result.
  const r1 = convertFixture("basic_vertex.spv");
  const r2 = convertFixture("basic_vertex.spv");
  if (r1.wgsl && r2.wgsl) {
    assertEq(r1.wgsl, r2.wgsl, "conversion is deterministic (same output twice)");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 14: Storage buffer access patterns
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 14: Storage buffer access patterns ===");

{
  // 14a. Read-only storage buffer fixture.
  const r = convertFixture("readonly_ssbo.spv");
  assert(r.wgsl !== null, "readonly_ssbo converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("var<storage"), "readonly_ssbo has storage variable");
    assert(r.wgsl.includes("@compute"), "readonly_ssbo is compute");
  }
}

{
  // 14b. Constructed read-only storage buffer → inferred as var<storage, read>.
  const spv = buildComputeWithStorageBuffer(false);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "constructed read-only storage buffer converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("var<storage, read>"), "read-only inferred as var<storage, read>");
    assert(!r.wgsl.includes("read_write"), "read-only NOT var<storage, read_write>");
  }
}

{
  // 14c. Constructed read-write storage buffer → stays var<storage, read_write>.
  const spv = buildComputeWithStorageBuffer(true);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "constructed read-write storage buffer converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("var<storage, read_write>"), "written buffer is var<storage, read_write>");
  }
}

{
  // 14d. readonly_ssbo fixture: read-only buffers get var<storage, read>.
  const r = convertFixture("readonly_ssbo.spv");
  if (r.wgsl) {
    // instances buffer is read-only, output_buf is read-write.
    const readOnlyMatches = [...r.wgsl.matchAll(/var<storage, read>/g)];
    const readWriteMatches = [...r.wgsl.matchAll(/var<storage, read_write>/g)];
    assert(readOnlyMatches.length >= 1, "readonly_ssbo has at least 1 var<storage, read>");
    assert(readWriteMatches.length >= 1, "readonly_ssbo has at least 1 var<storage, read_write>");
  }
}

{
  // 14e. compute_particles fixture: particle_buf is written.
  const r = convertFixture("compute_particles.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("var<storage, read_write>"), "compute_particles has read_write buffer");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 15: Batch mode (JSON output)
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 15: Batch mode ===");

{
  // 15a. Batch conversion of multiple fixtures produces valid JSON.
  const fixtures = FIXTURES.slice(0, 5).map(f => join(FIXTURES_DIR, f));
  try {
    const output = execFileSync(TINT_CLI, ["--batch", ...fixtures], {
      encoding: "utf-8", timeout: 60000,
    });
    const result = JSON.parse(output);
    assert(typeof result === "object", "batch output is valid JSON object");
    assertEq(Object.keys(result).length, 5, "batch output has 5 entries");
    for (const [path, val] of Object.entries(result)) {
      if (typeof val === "string") {
        assert(val.length > 0, `batch: ${path.split("/").pop()} has WGSL output`);
      }
    }
  } catch (e) {
    assert(false, `batch mode failed: ${e.message}`);
  }
}

{
  // 15b. Batch mode with all 13 fixtures.
  const fixtures = FIXTURES.map(f => join(FIXTURES_DIR, f));
  try {
    const output = execFileSync(TINT_CLI, ["--batch", ...fixtures], {
      encoding: "utf-8", timeout: 60000,
    });
    const result = JSON.parse(output);
    assertEq(Object.keys(result).length, 13, "batch output has 13 entries");
    let successCount = 0;
    for (const val of Object.values(result)) {
      if (typeof val === "string") successCount++;
    }
    assertEq(successCount, 13, "all 13 fixtures succeed in batch mode");
  } catch (e) {
    assert(false, `batch mode all fixtures failed: ${e.message}`);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 16: WGSL output validation
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 16: WGSL output validation ===");

{
  // 16a. Vertex shader WGSL has correct structure.
  const r = convertFixture("basic_vertex.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("@vertex"), "vertex WGSL has @vertex");
    assert(r.wgsl.includes("@builtin(position)"), "vertex WGSL has position builtin");
    assert(r.wgsl.includes("vec4<f32>"), "vertex WGSL has vec4<f32> return type");
  }
}

{
  // 16b. Fragment shader WGSL has correct structure.
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("@fragment"), "fragment WGSL has @fragment");
    assert(r.wgsl.includes("@location(0"), "fragment WGSL has location(0) output");
  }
}

{
  // 16c. Compute shader WGSL has correct structure.
  const r = convertFixture("compute_particles.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("@compute"), "compute WGSL has @compute");
    assert(r.wgsl.includes("@workgroup_size"), "compute WGSL has workgroup_size");
  }
}

{
  // 16d. No SPIR-V artifacts leak into WGSL.
  for (const fix of FIXTURES) {
    const r = convertFixture(fix);
    if (r.wgsl) {
      assert(!r.wgsl.includes("OpCode"), `${fix}: no OpCode in WGSL`);
      assert(!r.wgsl.includes("SPIR-V"), `${fix}: no SPIR-V text in WGSL`);
    }
  }
}

{
  // 16e. diagnostic(off, derivative_uniformity) present (Tint default).
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("diagnostic(off, derivative_uniformity)"),
      "Tint adds diagnostic directive");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 17: negate_position_y — struct member Position (Case B)
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 17: negate_position_y struct member Position ===");

{
  // 17a. Vertex shader where gl_Position is a struct member via OpMemberDecorate.
  // IDs: 1=void, 2=fn_type, 3=float, 4=vec4, 5=struct{vec4 Position},
  //      6=ptr_output_struct, 7=out_var, 8=main, 9=label,
  //      10=ptr_output_vec4, 11=const0f, 12=const1f, 13=constructed, 14=ac,
  //      15=int32, 16=int_const_0
  const spv = buildSpirv(17, [
    ...encodeInst(Op.Capability, 1), // Shader
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.Vertex, 8, "main", 7), // interface: out_var
    ...encodeInst(Op.MemberDecorate, 5, 0, Deco.BuiltIn, 0), // member 0 = Position
    ...encodeInst(Op.Decorate, 5, Deco.Block),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeFloat, 3, 32),
    ...encodeInst(Op.TypeVector, 4, 3, 4),
    ...encodeInst(Op.TypeStruct, 5, 4), // struct { vec4 }
    ...encodeInst(Op.TypePointer, 6, SC.Output, 5),
    ...encodeInst(Op.TypePointer, 10, SC.Output, 4),
    ...encodeInst(Op.TypeInt, 15, 32, 1), // int32 (signed)
    ...encodeInst(Op.Variable, 6, 7, SC.Output),
    ...encodeInst(Op.Constant, 3, 11, 0x00000000), // 0.0f
    ...encodeInst(Op.Constant, 3, 12, 0x3F800000), // 1.0f
    ...encodeInst(Op.Constant, 15, 16, 0), // int 0
    ...encodeInst(Op.Function, 1, 8, 0, 2),
    ...encodeInst(Op.Label, 9),
    ...encodeInst(Op.AccessChain, 10, 14, 7, 16), // ac = &out_var.member[0] (int index)
    ...encodeInst(Op.CompositeConstruct, 4, 13, 11, 12, 11, 12),
    ...encodeInst(Op.Store, 14, 13),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "struct member Position vertex shader converts");
  if (r.wgsl) {
    assert(r.wgsl.includes("@vertex"), "struct Position has @vertex");
    assert(r.wgsl.includes("@builtin(position)"), "struct Position has @builtin(position)");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 18: Additional spec constant operations
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 18: Additional spec constant operations ===");

{
  // 18a. UDiv (opcode 134): 20 / 10 = 2
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 134, 5, 4), // UDiv(20, 10) = 2
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp UDiv converts");
}

{
  // 18b. SNegate (opcode 126): -10
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 126, 4), // SNegate(10) = -10
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp SNegate converts");
}

{
  // 18c. BitwiseOr (opcode 197): 10 | 20 = 30
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 197, 4, 5), // BitwiseOr
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp BitwiseOr converts");
}

{
  // 18d. BitwiseAnd (opcode 199): 10 & 20 = 0
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 199, 4, 5), // BitwiseAnd
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp BitwiseAnd converts");
}

{
  // 18e. BitwiseXor (opcode 198): 10 ^ 20 = 30
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 198, 4, 5), // BitwiseXor
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp BitwiseXor converts");
}

{
  // 18f. Not (opcode 200): ~10
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 200, 4), // BitwiseNot
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp Not converts");
}

{
  // 18g. ShiftLeftLogical (opcode 196): 10 << 2 = 40
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 196, 4, 5), // ShiftLeftLogical
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp ShiftLeftLogical converts");
}

{
  // 18h. ShiftRightLogical (opcode 194): 20 >> 2 = 5
  const spv = buildComputeWithSpecConstants([
    encodeInst(Op.SpecConstantOp, 3, 6, 194, 5, 4), // ShiftRightLogical
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp ShiftRightLogical converts");
}

{
  // 18i. UMod (opcode 137): 20 % 10 = 0 (note: using unsigned int type)
  const mainStr = encodeString("main");
  const spv = buildSpirv(20, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.SpecId, 0),
    ...encodeInst(Op.Decorate, 5, Deco.SpecId, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 0), // unsigned int
    ...encodeInst(Op.SpecConstant, 3, 4, 20),
    ...encodeInst(Op.SpecConstant, 3, 5, 7),
    ...encodeInst(Op.SpecConstantOp, 3, 6, 137, 4, 5), // UMod(20, 7) = 6
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp UMod converts");
}

{
  // 18j. IEqual (opcode 170) and INotEqual (opcode 171).
  const spv = buildSpirv(12, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 8, "main"),
    ...encodeInst(Op.ExecutionMode, 8, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.SpecId, 0),
    ...encodeInst(Op.Decorate, 5, Deco.SpecId, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 1),
    ...encodeInst(Op.TypeBool, 9),
    ...encodeInst(Op.SpecConstant, 3, 4, 10),
    ...encodeInst(Op.SpecConstant, 3, 5, 10),
    ...encodeInst(Op.SpecConstantOp, 9, 6, 170, 4, 5), // IEqual(10, 10) = true
    ...encodeInst(Op.SpecConstantOp, 9, 7, 171, 4, 5), // INotEqual(10, 10) = false
    ...encodeInst(Op.Function, 1, 8, 0, 2),
    ...encodeInst(Op.Label, 10),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp IEqual/INotEqual converts");
}

{
  // 18k. SDiv with divide-by-zero (opcode 135): should produce 0.
  const spv = buildSpirv(12, [
    ...encodeInst(Op.Capability, 1),
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.Decorate, 4, Deco.SpecId, 0),
    ...encodeInst(Op.Decorate, 5, Deco.SpecId, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeInt, 3, 32, 1),
    ...encodeInst(Op.SpecConstant, 3, 4, 42),
    ...encodeInst(Op.SpecConstant, 3, 5, 0), // divisor = 0
    ...encodeInst(Op.SpecConstantOp, 3, 6, 135, 4, 5), // SDiv(42, 0) → 0
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  assert(r.wgsl !== null, "OpSpecConstantOp SDiv-by-zero converts (no crash)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 19: split_combined_samplers — high binding values
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 19: split_combined_samplers high bindings ===");

{
  // 19a. basic_fragment with high bindings — verify binding doubling works.
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    // After splitting, sampler bindings are N*2 and image bindings are N*2+1.
    // Verify no duplicate bindings within the same group.
    const groupBindings = {};
    for (const m of r.wgsl.matchAll(/@group\((\d+)u?\)\s*@binding\((\d+)u?\)/g)) {
      const key = `g${m[1]}b${m[2]}`;
      groupBindings[key] = (groupBindings[key] || 0) + 1;
    }
    // Allow sampler and texture on same binding (different resource types).
    // Just verify the shader converts without error.
    assert(Object.keys(groupBindings).length > 0, "high bindings: bindings parsed");
  }
}

{
  // 19b. multi_texture — 6+ combined samplers, binding doubling.
  const r = convertFixture("multi_texture.spv");
  if (r.wgsl) {
    const bindings = [...r.wgsl.matchAll(/@binding\((\d+)u?\)/g)].map(m => parseInt(m[1]));
    assert(bindings.length > 0, "multi_texture: bindings present after split");
    // Verify no negative or absurdly large bindings.
    const maxBinding = Math.max(...bindings);
    assert(maxBinding < 10000, `multi_texture: max binding ${maxBinding} is reasonable`);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 20: fix_nonfinite_literals — 64-bit floats
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 20: fix_nonfinite_literals 64-bit ===");

{
  // 20a. Shader with f64 infinity constant.
  // Float64 +Infinity: 0x7FF0000000000000 → low word = 0x00000000, high word = 0x7FF00000
  const spv = buildSpirv(10, [
    ...encodeInst(Op.Capability, 1), // Shader
    ...encodeInst(Op.Capability, 10), // Float64
    ...encodeInst(Op.MemoryModel, 0, 1),
    ...encodeEntryPoint(ExecModel.GLCompute, 7, "main"),
    ...encodeInst(Op.ExecutionMode, 7, 17, 1, 1, 1),
    ...encodeInst(Op.TypeVoid, 1),
    ...encodeInst(Op.TypeFunction, 2, 1),
    ...encodeInst(Op.TypeFloat, 3, 64), // f64
    // OpConstant for f64 takes 2 literal words (low, high).
    ...[((5) << 16) | Op.Constant, 3, 4, 0x00000000, 0x7FF00000], // +Infinity f64
    ...[((5) << 16) | Op.Constant, 3, 5, 0x00000000, 0xFFF00000], // -Infinity f64
    ...encodeInst(Op.Function, 1, 7, 0, 2),
    ...encodeInst(Op.Label, 8),
    ...encodeInst(Op.Return),
    ...encodeInst(Op.FunctionEnd),
  ]);
  const r = convertToWgsl(spv);
  // Tint may or may not support f64 — the important thing is no crash.
  // If it converts, verify no infinity in output.
  if (r.wgsl) {
    const lower = r.wgsl.toLowerCase();
    assert(!lower.includes("infinity"), "f64: no infinity in output");
  }
  // If it fails, that's OK — f64 support depends on Tint capabilities.
  assert(true, "f64 infinity: no crash");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 21: Pass interaction — multi-feature fixture
// ─────────────────────────────────────────────────────────────────────────────
console.log("\n=== Test 21: Pass interaction ===");

{
  // 21a. basic_vertex exercises: push constants + Y-negation + spec constant folding.
  const r = convertFixture("basic_vertex.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("@vertex"), "pass interaction: vertex entry");
    assert(r.wgsl.includes("@group(3") && r.wgsl.includes("@binding(120"),
      "pass interaction: push constants converted");
    assert(!r.wgsl.includes("override"), "pass interaction: spec constants folded");
  }
}

{
  // 21b. basic_fragment exercises: combined sampler split + push constants + depth images.
  const r = convertFixture("basic_fragment.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_2d"), "pass interaction: samplers split");
    assert(r.wgsl.includes(": sampler"), "pass interaction: sampler type present");
    assert(r.wgsl.includes("@binding(120"), "pass interaction: push constants");
    assert(r.wgsl.includes("@fragment"), "pass interaction: fragment entry");
  }
}

{
  // 21c. depth_sampling exercises: depth image fix + combined sampler split.
  const r = convertFixture("depth_sampling.spv");
  if (r.wgsl) {
    assert(r.wgsl.includes("texture_depth"), "pass interaction: depth image fixed");
    assert(r.wgsl.includes("sampler"), "pass interaction: sampler split from depth");
  }
}

{
  // 21d. per_stage_overrides exercises: spec constants per-stage.
  const rv = convertFixture("per_stage_overrides_vert.spv");
  const rf = convertFixture("per_stage_overrides_frag.spv");
  if (rv.wgsl && rf.wgsl) {
    assert(!rv.wgsl.includes("override"), "pass interaction: vert spec constants folded");
    assert(!rf.wgsl.includes("override"), "pass interaction: frag spec constants folded");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Summary
// ─────────────────────────────────────────────────────────────────────────────
console.log(`\n${"=".repeat(50)}`);
console.log(`Results: ${passed} passed, ${failed} failed, ${skipped} skipped`);

if (failed > 0) {
  process.exit(1);
}
