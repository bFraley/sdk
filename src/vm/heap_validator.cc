// Copyright (c) 2015, the Dartino project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include "src/vm/heap_validator.h"
#include "src/vm/process.h"

namespace dartino {

void HeapPointerValidator::VisitBlock(Object** start, Object** end) {
  for (; start != end; start++) {
    ValidatePointer(*start);
  }
}

void HeapPointerValidator::ValidatePointer(Object* object) {
  if (!object->IsHeapObject()) return;

  HeapObject* heap_object = HeapObject::cast(object);
  uword address = heap_object->address();

  bool is_process_heap_obj = false;
  if (process_heap_ != NULL) {
    is_process_heap_obj = (process_heap_->space()->Includes(address) ||
                           process_heap_->old_space()->Includes(address));
  }

  bool is_program_heap = program_heap_->space()->Includes(address);

  if (!is_process_heap_obj && !is_program_heap &&
      !StaticClassStructures::IsStaticClass(heap_object)) {
    fprintf(stderr,
            "Found pointer %p which lies in neither of "
            "mutable_heap/program_heap.\n",
            heap_object);

    FATAL("Heap validation failed.");
  }

  Class* klass = heap_object->get_class();
  bool valid_class = program_heap_->space()->Includes(klass->address()) ||
                     StaticClassStructures::IsStaticClass(klass);
  if (!valid_class) {
    fprintf(stderr, "Object %p had an invalid klass pointer %p\n", heap_object,
            klass);
    FATAL("Heap validation failed.");
  }
}

void ProcessRootValidatorVisitor::VisitProcess(Process* process) {
  TwoSpaceHeap* process_heap = process->heap();
  // Validate pointers in roots and queues.
  HeapPointerValidator validator(program_heap_, process_heap);
  HeapObjectPointerVisitor pointer_visitor(&validator);
  process->IterateRoots(&validator);
  process->mailbox()->IteratePointers(&validator);
}

}  // namespace dartino
