# Builds a dlopen-able shared liblogos_protocol.{so,dylib} exposing the lp_* C
# ABI, for FFI/dlopen SDKs (js-sdk via koffi, or any language runtime that
# loads the C ABI). Same sources + deps as the static lib, built with
# LOGOS_PROTOCOL_BUILD_SHARED=ON. Nix's Qt wrapper stamps the RPATH so the
# shared object resolves its Qt / Boost / OpenSSL / spdlog deps at dlopen time
# without the loader needing them on a system path.
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-shared";
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs meta;
  buildInputs = common.buildInputs;
  propagatedBuildInputs = common.propagatedBuildInputs;

  dontUseCmakeConfigure = true;

  cmakeFlags = common.cmakeFlags ++ [ "-DLOGOS_PROTOCOL_BUILD_SHARED=ON" ];

  buildPhase = ''
    runHook preBuild

    mkdir -p build-protocol-shared
    cd build-protocol-shared
    cmake ../cpp -DCMAKE_INSTALL_PREFIX=$out $cmakeFlags
    ninja
    cd ..

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    # Installs the static lib + cmake config + the shared lib. The shared
    # object (liblogos_protocol.{so,dylib}) is the artifact FFI SDKs load.
    cmake --install build-protocol-shared

    runHook postInstall
  '';
}
