// Copyright (c) 2015, the Dartino project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(DARTINO_TARGET_IA32) && defined(DARTINO_TARGET_OS_MACOS)

#include <stdio.h>
#include "src/vm/assembler.h"

namespace dartino {

const char* kLocalLabelPrefix = "L";

#if defined(DARTINO_TARGET_ANDROID)
static const char* kPrefix = "";
#else
static const char* kPrefix = "_";
#endif

void Assembler::call(const char* name) {
  printf("\tcall %s%s\n", kPrefix, name);
}

void Assembler::j(Condition condition, const char* name) {
  const char* mnemonic = ConditionMnemonic(condition);
  printf("\tj%s %s%s\n", mnemonic, kPrefix, name);
}

void Assembler::jmp(const char* name) { printf("\tjmp %s%s\n", kPrefix, name); }

void Assembler::jmp(const char* name, Register index, ScaleFactor scale) {
  Print("jmp *%s%s(,%rl,%d)", kPrefix, name, index, 1 << scale);
}

void Assembler::Bind(const char* prefix, const char* name) {
  putchar('\n');
  printf("\t.globl %s%s%s\n", kPrefix, prefix, name);
  printf("%s%s%s:\n", kPrefix, prefix, name);
}

void Assembler::DefineLong(const char* name) {
  printf("\t.long %s%s\n", kPrefix, name);
}

void Assembler::LoadNative(Register destination, Register index) {
  Print("movl %skNativeTable(,%rl,4), %rl", kPrefix, index, destination);
}

void Assembler::LoadLabel(Register reg, const char* name) {
  Print("leal %s%s, %rl", kPrefix, name, reg);
}

}  // namespace dartino

#endif  // defined DARTINO_TARGET_IA32 && defined(DARTINO_TARGET_OS_MACOS)
