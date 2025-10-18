project('luna','c',
        default_options:['c_std=c2x']
)

includedir=include_directories('include')

ds_srcs = ['src/stb_ds.c','src/arena.c']
parser_srcs=['src/parser/tokenizer.c','src/parser/parser.c','src/parser/ast.c']

runtime_srcs=['src/runtime/objects/integer.c','src/runtime/eval.c',
                'src/runtime/heap.c','src/runtime/istate.c',
                'src/runtime/objects/boolean.c','src/runtime/objects/strobj.c'
                ,'src/strings/fly_string.c','src/strings/interner.c','src/runtime/luerrors.c',
                'src/runtime/gc_objectset.c','src/runtime/object.c','src/runtime/worklist.c',
                'src/runtime/objects/hashmap.c','src/runtime/objects/function.c','src/runtime/objects/native_function.c']

main_srcs=['src/main.c']

luna_runtime=shared_library('lunarun',
    parser_srcs+ds_srcs+runtime_srcs,
    include_directories:includedir
)

executable('luna',
    'src/main.c',
    include_directories:includedir,
    link_with:[luna_runtime]
)
