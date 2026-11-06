{
  description = "gm_mcp — Garry's Mod MCP bridge (server module + Bun MCP server)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      # GMod Linux modules are x86/x86_64 only.
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      mkGmMcp =
        {
          stdenv,
          cmake,
          nlohmann_json,
          lib,
          pnameSuffix ? "",
          metaPlatforms ? lib.platforms.linux,
        }:
        stdenv.mkDerivation {
          pname = "gm_mcp${pnameSuffix}";
          version = "0.1.0";

          src = lib.cleanSource ./.;

          nativeBuildInputs = [ cmake ];
          buildInputs = [ nlohmann_json ];

          cmakeBuildType = "Release";

          cmakeFlags = [ "-DGM_MCP_TARGET=auto" ];

          installPhase = ''
            runHook preInstall
            mkdir -p $out/lib
            cp -v gmsv_mcp_*.dll $out/lib/
            runHook postInstall
          '';

          meta = with lib; {
            description = "MCP bridge binary module for Garry's Mod (server-only)";
            license = licenses.gpl3Plus;
            platforms = metaPlatforms;
          };
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          linux64 = pkgs.callPackage mkGmMcp {
            pnameSuffix = "-linux64";
            metaPlatforms = [ "x86_64-linux" ];
          };

          linux32 = pkgs.pkgsi686Linux.callPackage mkGmMcp {
            pnameSuffix = "-linux32";
            metaPlatforms = [ "i686-linux" ];
          };

          mcp = pkgs.callPackage ./mcp {
            # Bump when mcp/bun.lock or package.json dependencies change.
            nodeModulesHash = "sha256-9LHdtdnnH8su7sLcNx2dmuQi0vSV5jfsxnVvBiOvggA=";
          };

          all = pkgs.symlinkJoin {
            name = "gm_mcp-all";
            paths = [
              linux64
              linux32
            ];
            meta = {
              description = "gm_mcp GMod modules (gmsv_mcp, linux32 + linux64)";
            };
          };
        in
        {
          inherit linux64 linux32 mcp all;
          default = all;
        }
      );

      apps = forAllSystems (
        system:
        let
          pkgs = self.packages.${system};
        in
        {
          mcp = {
            type = "app";
            program = "${pkgs.mcp}/bin/gm-mcp";
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          queryDriver = "${pkgs.stdenv.cc}/bin/*";
          clangdForNix = pkgs.writeShellScriptBin "clangd" ''
            exec ${pkgs.clang-tools}/bin/clangd --query-driver=${queryDriver} "$@"
          '';
        in
        {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.ninja
              pkgs.gcc
              pkgs.pkgsi686Linux.gcc
              pkgs.nlohmann_json
              clangdForNix
              pkgs.clang-tools
              pkgs.bun
            ];

            shellHook = ''
              if [ -f build/linux64/compile_commands.json ]; then
                ln -sfn build/linux64/compile_commands.json compile_commands.json
              fi
            '';
          };

          mcp = pkgs.mkShell {
            packages = [ pkgs.bun ];
            shellHook = ''
              cd "${toString ./mcp}"
              if [ ! -d node_modules ]; then
                bun install --frozen-lockfile
              fi
              echo "gm-mcp Bun shell — try: bun run dev | bun test | bun run build"
            '';
          };
        }
      );
    };
}
