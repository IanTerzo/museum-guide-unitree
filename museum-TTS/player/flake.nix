{
  description = "museumTTS audio playback server";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        pkg = pkgs.buildGoModule {
          pname = "museumTTS";
          version = "0.1.0";
          src = ./.;
          vendorHash = null;

          nativeBuildInputs = [ pkgs.makeWrapper ];

          postInstall = ''
            wrapProgram $out/bin/museumTTS \
              --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.ffmpeg ]}
          '';

          meta = {
            description = "HTTP server that receives audio files and plays them";
            mainProgram = "museumTTS";
          };
        };
      in {
        packages.default = pkg;

        apps.default = {
          type = "app";
          program = toString (pkgs.writeShellScript "run-museumTTS" ''
            exec ${pkg}/bin/museumTTS "$@"
          '');
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            go
            ffmpeg
          ];
        };
      }
    );
}
