"""Rules that drive the @llvm toolchain's clang the way non-Bazel build systems do.

Both `rules_foreign_cc` (CMake/autotools) and `rules_rust` (`cargo_build_script`
+ cc-rs) do not compile C code through Bazel's own C/C++ actions. Instead they
ask the C++ toolchain for its command line via
`cc_common.get_memory_inefficient_command_line` and hand the result to the
external build system as `CFLAGS` / `LDFLAGS`. The two tiny rules below replay
exactly that pattern so the sanitizer problems it exposes can be reproduced
without pulling in CMake or Cargo.
"""

load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

def _toolchain(ctx):
    cc_toolchain = find_cc_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    return cc_toolchain, feature_configuration

def _compile_flags(cc_toolchain, feature_configuration, user_compile_flags):
    # Mirrors rules_rust `get_cc_compile_args_and_env`: no `source_file` and no
    # `output_file` variables, the build script supplies those itself.
    variables = cc_common.create_compile_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        user_compile_flags = user_compile_flags,
    )
    flags = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.c_compile,
        variables = variables,
    )
    env = cc_common.get_environment_variables(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.c_compile,
        variables = variables,
    )
    return flags, env

def _link_flags(cc_toolchain, feature_configuration, user_link_flags):
    # Mirrors rules_foreign_cc `get_flags_info`, which becomes
    # CMAKE_EXE_LINKER_FLAGS for CMake's compiler sanity check.
    variables = cc_common.create_link_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        is_linking_dynamic_library = False,
        user_link_flags = user_link_flags,
    )
    return cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.cpp_link_executable,
        variables = variables,
    )

def _run_clang(ctx, cc_toolchain, feature_configuration, env, args, output, mnemonic):
    clang = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.c_compile,
    )
    ctx.actions.run_shell(
        inputs = depset(ctx.files.srcs, transitive = [cc_toolchain.all_files]),
        outputs = [output],
        tools = [],
        env = env,
        command = " ".join(["\"%s\"" % clang] + args),
        mnemonic = mnemonic,
        progress_message = "%s %s" % (mnemonic, ctx.label),
    )

def _foreign_cc_compiler_check_impl(ctx):
    """CMake's `try_compile` / compiler sanity check: `clang CFLAGS LDFLAGS a.c -o a.out`.

    CMake compiles and links a trivial C program with the flags it was handed
    before configuring the real project (cyclonedds in our case). It is a *C*
    link: nothing adds libc++/libc++abi, so every C++ symbol the sanitizer
    runtime drags in is unresolved.
    """
    cc_toolchain, feature_configuration = _toolchain(ctx)
    cflags, env = _compile_flags(
        cc_toolchain,
        feature_configuration,
        ctx.fragments.cpp.copts + ctx.fragments.cpp.conlyopts,
    )
    ldflags = _link_flags(cc_toolchain, feature_configuration, ctx.fragments.cpp.linkopts)
    out = ctx.actions.declare_file(ctx.label.name)
    _run_clang(
        ctx,
        cc_toolchain,
        feature_configuration,
        env,
        ["'%s'" % f for f in cflags + ldflags] +
        [src.path for src in ctx.files.srcs] +
        ["-o", out.path],
        out,
        "ForeignCcCompilerCheck",
    )
    return [DefaultInfo(files = depset([out]), executable = out)]

foreign_cc_compiler_check = rule(
    implementation = _foreign_cc_compiler_check_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".c"], mandatory = True),
    },
    executable = True,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cargo_build_script_cc_object_impl(ctx):
    """cc-rs inside a `cargo_build_script`: `clang CFLAGS -c $PWD/src.c -o src.o`.

    cc-rs always passes *absolute* source paths and rules_rust later rejects any
    artifact that embeds the absolute execroot ("rlib embeds the absolute
    working directory").
    """
    cc_toolchain, feature_configuration = _toolchain(ctx)
    cflags, env = _compile_flags(cc_toolchain, feature_configuration, [])
    src = ctx.files.srcs[0]
    out = ctx.actions.declare_file(ctx.label.name + ".o")
    _run_clang(
        ctx,
        cc_toolchain,
        feature_configuration,
        env,
        ["'%s'" % f for f in cflags] +
        ["-fPIC", "-c", "\"$(pwd)/%s\"" % src.path, "-o", out.path],
        out,
        "CargoBuildScriptCc",
    )
    return [DefaultInfo(files = depset([out]))]

cargo_build_script_cc_object = rule(
    implementation = _cargo_build_script_cc_object_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".c"], mandatory = True, allow_empty = False),
    },
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
