{
  description = "museumTTS web UI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        pkg = pkgs.buildGoModule {
          pname = "museumTTS-ui";
          version = "0.1.0";
          src = ./.;
          vendorHash = null;

          meta = {
            description = "Web UI for museumTTS";
            mainProgram = "ui";
          };
        };
      in {
        packages.default = pkg;

        apps.default = {
          type = "app";
          program = "${pkg}/bin/ui";
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [ pkgs.go ];
        };
      }
    );
}
