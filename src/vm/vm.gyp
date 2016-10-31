# Copyright (c) 2015, the Dartino project authors. Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE.md file.

{
  'target_defaults': {
    'include_dirs': [
      '../../',
    ],
  },
  'targets': [
    # The split between runtime and interpreter is somewhat arbitrary. The goal
    # is to be able to build a host version of the runtime part that suffices
    # to build the flashtool helper. So as long as flashtool still builds in
    # a crosscompilation setting it does not matter where a new file goes.
    {
      'target_name': 'dartino_vm_runtime_library',
      'type': 'static_library',
      'toolsets': ['target', 'host'],
      'target_conditions': [
        ['_toolset == "target"', {
          'standalone_static_library': 1,
      }]],
      'dependencies': [
        '../shared/shared.gyp:dartino_shared',
        '../double_conversion.gyp:double_conversion',
      ],
      'conditions': [
        [ 'OS=="mac"', {
          'dependencies': [
            '../shared/shared.gyp:copy_asan#host',
          ],
          'sources': [
            '<(PRODUCT_DIR)/libclang_rt.asan_osx_dynamic.dylib',
          ],
        }],
        ['OS!="win" and posix==1', {
          'link_settings': {
            'libraries': [
              '-lpthread',
              '-ldl',
              # TODO(ahe): Not sure this option works as intended on Mac.
              '-rdynamic',
            ],
          },
        }],
      ],
      'include_dirs': [
          '../../third_party/llvm/llvm-build/include',
          '../../third_party/llvm/include',
      ],
      'sources': [
        'dartino_api_impl.cc',
        'dartino_api_impl.h',
        'dartino.cc',
        'debug_info.cc',
        'debug_info.h',
        'debug_info_no_live_coding.h',
        'double_list.h',
        'dartino_api_impl.cc',
        'dartino_api_impl.h',
        'dartino.cc',
        'event_handler_lk.cc',
        'event_handler_windows.cc',
        'event_handler_macos.cc',
        'event_handler_cmsis.cc',
        'event_handler_linux.cc',
        'event_handler_posix.cc',
        'event_handler.cc',
        'gc_llvm.cc',
        'gc_llvm.h',
        'gc_thread.cc',
        'gc_thread.h',
        'hash_map.h',
        'hash_set.h',
        'hash_table.h',
        'heap.cc',
        'heap.h',
        'heap_validator.cc',
        'heap_validator.h',
        'intrinsics.cc',
        'intrinsics.h',
        'links.cc',
        'links.h',
        'llvm_eh.cc',
        'llvm_eh.h',
        'log_print_interceptor.cc',
        'log_print_interceptor.h',
        'lookup_cache.cc',
        'lookup_cache.h',
        'mailbox.h',
        'message_mailbox.cc',
        'message_mailbox.h',
        'multi_hashset.h',
        'native_process_disabled.cc',
        'native_process_posix.cc',
        'native_process_windows.cc',
        'natives.cc',
        'natives_cmsis.cc',
        'natives.h',
        'natives_lk.cc',
        'natives_posix.cc',
        'natives_windows.cc',
        'object.cc',
        'object.h',
        'object_list.cc',
        'object_list.h',
        'object_map.cc',
        'object_map.h',
        'object_memory.cc',
        'object_memory_copying.cc',
        'object_memory.h',
        'object_memory_mark_sweep.cc',
        'pair.h',
        'port.cc',
        'port.h',
        'priority_heap.h',
        'process.cc',
        'process.h',
        'process_handle.cc',
        'process_handle.h',
        'process_queue.h',
        'program.cc',
        'program_folder.cc',
        'program_folder.h',
        'program_groups.cc',
        'program_groups.h',
        'program.h',
        'program_info_block.cc',
        'program_info_block.h',
        'remembered_set.h',
        'scheduler.cc',
        'scheduler.h',
        'selector_row.cc',
        'selector_row.h',
        'service_api_impl.cc',
        'service_api_impl.h',
        'session.cc',
        'session.h',
        'session_no_live_coding.h',
        'signal.h',
        'snapshot.cc',
        'snapshot.h',
        'sort.cc',
        'sort.h',
        'thread_cmsis.cc',
        'thread_cmsis.h',
        'thread.h',
        'thread_lk.cc',
        'thread_lk.h',
        'thread_pool.cc',
        'thread_pool.h',
        'thread_posix.cc',
        'thread_posix.h',
        'thread_windows.cc',
        'thread_windows.h',
        'unicode.cc',
        'unicode.h',
        'vector.cc',
        'vector.h',
        'void_hash_table.cc',
        'void_hash_table.h',
        'weak_pointer.cc',
        'weak_pointer.h',
      ],
    },
    {
      'target_name': 'dartino_vm_interpreter_library',
      'type': 'static_library',
      'toolsets': ['target'],
      'standalone_static_library': 1,
      'conditions': [
        [ 'OS=="mac"', {
          'dependencies': [
            '../shared/shared.gyp:copy_asan#host',
          ],
          'sources': [
            '<(PRODUCT_DIR)/libclang_rt.asan_osx_dynamic.dylib',
          ],
        }],
        [ 'OS=="win"', {
          'variables': {
            'asm_file_extension': '.asm',
          },
        }],
      ],
      'variables': {
        'asm_file_extension%': '.S',
        'yasm_output_path': '<(INTERMEDIATE_DIR)',
        'yasm_flags': [
          '-f', 'win32',
          '-m', 'x86',
          '-p', 'gas',
          '-r', 'raw',
        ],
      },
      'includes': [
        '../../third_party/yasm/yasm_compile.gypi'
      ],
      'sources': [
        'ffi.cc',
        'ffi_disabled.cc',
        'ffi.h',
        'ffi_linux.cc',
        'ffi_macos.cc',
        'ffi_posix.cc',
        'ffi_static.cc',
        'ffi_windows.cc',
        'interpreter.cc',
        'interpreter.h',
        'native_interpreter.cc',
        'native_interpreter.h',
        'preempter.cc',
        'preempter.h',
        'tick_queue.h',
        'tick_sampler.h',
        'tick_sampler_other.cc',
        'tick_sampler_posix.cc',
      ]
    },
    {
      'target_name': 'libdartino',
      'type': 'none',
      'dependencies': [
        'dartino_vm_runtime_library',
        'dartino_vm_interpreter_library',
        '../shared/shared.gyp:dartino_shared',
        '../double_conversion.gyp:double_conversion',
      ],
      'sources': [
        # TODO(ager): Lint target default requires a source file. Not
        # sure how to work around that.
        'assert.h',
      ],
      'actions': [
        {
          'action_name': 'generate_libdartino',
          'inputs': [
            '../../tools/library_combiner.py',
            '<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)dartino_vm_runtime_library'
                '<(STATIC_LIB_SUFFIX)',
            '<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)dartino_vm_interpreter_library'
                '<(STATIC_LIB_SUFFIX)',
            '<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)dartino_shared'
                '<(STATIC_LIB_SUFFIX)',
            '<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)double_conversion'
                '<(STATIC_LIB_SUFFIX)',
          ],
          'outputs': [
            '<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)dartino<(STATIC_LIB_SUFFIX)',
          ],
          'action': [
            'python', '<@(_inputs)', '<@(_outputs)',
          ]
        },
      ]
    },
    {
      'target_name': 'llvm-codegen',
      'type': 'executable',

      # $ llvm-config-3.6  --cxxflags --ldflags --system-libs --libs core scalaropts
      'defines': [
          'D_GNU_SOURCE',
          'D__STDC_CONSTANT_MACROS',
          'D__STDC_FORMAT_MACROS',
          'D__STDC_LIMIT_MACROS',
      ],
      'include_dirs': [
          #'/usr/lib/llvm-3.6/include',
          '../../third_party/llvm/llvm-build/include',
          '../../third_party/llvm/include',
      ],
      'libraries': [
          #'-L/usr/lib/llvm-3.6/lib',
          '-L../../third_party/llvm/llvm-build/lib',

          '-Wl,--start-group',

          # core
          '-lLLVMCore',
          '-lLLVMSupport',
          '-lLLVMBitWriter',

          # scalaropts (contains mem2reg pass, ...)
          '-lLLVMAnalysis',
          '-lLLVMBitReader',
          '-lLLVMCodeGen',
          '-lLLVMCore',
          '-lLLVMInstCombine',
          #'-lLLVMipa',
          '-lLLVMMC',
          '-lLLVMMCParser',
          '-lLLVMObject',
          '-lLLVMProfileData',
          '-lLLVMScalarOpts',
          '-lLLVMSupport',
          '-lLLVMTarget',
          '-lLLVMTransformUtils',

          '-Wl,--end-group',

          # misc
          '-lz',
          '-lpthread',
          '-lffi',
          '-ledit',
          '-ltinfo',
          '-ldl',
          '-lm',
      ],

      'dependencies' : [
        'libdartino',
      ],
      'sources': [
        'codegen_llvm.h',
        'codegen_llvm.cc',
        'codegen_llvm_main.cc',
      ],
    },
    {
      'target_name': 'dartino-vm',
      'type': 'executable',
      'dependencies': [
        'libdartino',
      ],
      'sources': [
        'main.cc',
        'main_simple.cc',
      ],
    },
    {
      'target_name': 'llvm_embedder',
      'type': 'static_library',
      'dependencies': [
        'libdartino',
      ],
      'sources': [
        'llvm_embedder.cc',
      ],
    },
    {
      'target_name': 'vm_cc_tests',
      'type': 'executable',
      'dependencies': [
        'libdartino',
        '../shared/shared.gyp:cc_test_base',
      ],
      'defines': [
        'TESTING',
        # TODO(ahe): Remove this when GYP is the default.
        'GYP',
      ],
      'sources': [
        # TODO(ahe): Add header (.h) files.
        'double_list_tests.cc',
        'hash_table_test.cc',
        'object_map_test.cc',
        'object_memory_test.cc',
        'object_test.cc',
        'platform_test.cc',
        'priority_heap_test.cc',
        'vector_test.cc',
      ],
    },
    {
      'target_name': 'multiprogram_cc_test',
      'type': 'executable',
      'dependencies': [
        'libdartino',
      ],
      'defines': [
        'TESTING',
      ],
      'sources': [
        'multiprogram_test.cc',
      ],
    },
    {
      'target_name': 'ffi_test_local_library',
      'type': 'shared_library',
      'dependencies': [
      ],
      'sources': [
        'ffi_test_library.h',
        'ffi_test_library.c',
      ],
      'cflags': ['-fPIC'],
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [
          '-fPIC',
        ],
      },
    },
    {
      'target_name': 'ffi_test_library',
      'type': 'shared_library',
      'dependencies': [
      ],
      'sources': [
        'ffi_test_library.h',
        'ffi_test_library.c',
      ],
      'cflags': ['-fPIC'],
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [
          '-fPIC',
        ],
      },
    },
    {
      'target_name': 'dartino_relocation_library',
      'type': 'static_library',
      'toolsets': ['target', 'host'],
      'target_conditions': [
        ['_toolset == "target"', {
          'standalone_static_library': 1,
      }]],
      'sources': [
        'dartino_relocation_api_impl.cc',
        'dartino_relocation_api_impl.h',
        'program_info_block.h',  # only to detect interface changes
        'program_relocator.cc',
        'program_relocator.h',
      ],
    },
  ],
}
