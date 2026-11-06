{
  lib,
  stdenvNoCC,
  bun,
  makeWrapper,
  nodeModulesHash ? "",
}:

let
  src = lib.cleanSource ./.;

  # Offline-friendly install of node_modules from bun.lock.
  nodeModules = stdenvNoCC.mkDerivation {
    name = "gm-mcp-node-modules";
    inherit src;

    nativeBuildInputs = [ bun ];

    dontConfigure = true;
    dontFixup = true;

    buildPhase = ''
      runHook preBuild
      export HOME=$TMPDIR
      bun install --frozen-lockfile
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -R node_modules $out/
      cp bun.lock package.json $out/
      runHook postInstall
    '';

    outputHashAlgo = "sha256";
    outputHashMode = "recursive";
    outputHash =
      if nodeModulesHash != "" then
        nodeModulesHash
      else
        lib.fakeHash;
  };
in
stdenvNoCC.mkDerivation {
  pname = "gm-mcp";
  version = "0.1.0";

  inherit src;

  nativeBuildInputs = [
    bun
    makeWrapper
  ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    export HOME=$TMPDIR
    cp -R ${nodeModules}/node_modules .
    chmod -R u+w node_modules
    bun run build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib/gm-mcp $out/bin
    cp -R dist package.json ${nodeModules}/node_modules $out/lib/gm-mcp/
    makeWrapper ${lib.getExe bun} $out/bin/gm-mcp \
      --chdir "$out/lib/gm-mcp" \
      --add-flags "run ./dist/server.js"
    runHook postInstall
  '';

  meta = with lib; {
    description = "Model Context Protocol server for Garry's Mod";
    license = licenses.gpl3Plus;
    mainProgram = "gm-mcp";
    platforms = platforms.linux;
  };
}
