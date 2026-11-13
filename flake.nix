{
  description = "CLBOSS Automated Core Lightning Node Manager";
  nixConfig.bash-prompt = "\[nix-develop\]$ ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      flake-compat,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        nativeBuildInputs = [
          pkgs.autoconf-archive
          pkgs.autoreconfHook
          pkgs.pkg-config
	];
        buildInputs = [
          pkgs.libev
          pkgs.libunwind
          pkgs.curlWithGnuTls
          pkgs.sqlite
        ];
        clboss = pkgs.stdenv.mkDerivation {
          name = "clboss";
          src = ./.;
          inherit nativeBuildInputs;
          inherit buildInputs;

          enableParallelBuilding = true;

          # There is no git in the build sandbox and no .git in the
          # source, so generate_commit_hash.sh has nothing to ask: take
          # the commit from the flake instead.  The Makefile keeps a
          # commit_hash.h it finds outside a git work tree.
          preBuild = ''
            printf '#pragma once\n#define GIT_COMMIT_HASH "%s"\n#define GIT_DESCRIBE "%s"\n' \
              "${self.rev or self.dirtyRev or "unknown"}" \
              "nix-${self.shortRev or self.dirtyShortRev or "unknown"}" \
              > commit_hash.h
          '';

          doCheck = false;

          meta = with pkgs.lib; {
            description = "Automated Core Lightning Node Manager";
            homepage = "https://github.com/ZmnSCPxj/clboss";
            license = licenses.mit;
            platforms = platforms.linux ++ platforms.darwin;
            mainProgram = "clboss";
          };
        };
      in
      {
        packages.default = clboss;
        checks.default = clboss.overrideAttrs (old: {
          doCheck = true;
          checkPhase = ''
            make -j4 distcheck
          '';
        });
        formatter = pkgs.nixfmt-tree;

        devShells.default = pkgs.mkShell {
          inherit nativeBuildInputs;
          buildInputs = buildInputs ++ (with pkgs; [
            gcc
            bind
            autoconf
            libtool
            automake
            git
            # editor support
            bear
          ]);
        };
      }
    );
}
