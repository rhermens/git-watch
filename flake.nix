{
  description = "git-fsnotify";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    crane.url = "github:ipetkov/crane";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, crane }:
    let
      homeManagerModule = { config, lib, pkgs, ... }:
        let
          cfg = config.services.git-fsnotify;
          enabledServices = lib.filterAttrs (_: service: service.enable) cfg;
          git-fsnotify = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        in
        {
          options = {
            services.git-fsnotify = lib.mkOption {
              type = lib.types.attrsOf (lib.types.submodule {
                options = {
                  enable = lib.mkEnableOption "git-fsnotify";

                  path = lib.mkOption {
                    type = lib.types.either lib.types.path lib.types.str;
                    default = lib.mkHomeDirPath "git-fsnotify";
                    description = "Path to watch";
                  };

                  logLevel = lib.mkOption {
                    type = lib.types.enum [ "trace" "debug" "info" "warn" "error" ];
                    default = "info";
                    description = "Log level";
                  };
                };
              });
              default = { };
              description = "git-fsnotify service instances.";
            };
          };

          config = lib.mkIf (enabledServices != { }) (lib.mkMerge [
            {
              home.packages = [ git-fsnotify ];
            }

            (lib.mkIf pkgs.stdenv.isLinux {
              systemd.user.services = lib.mapAttrs'
                (name: service:
                  lib.nameValuePair "git-fsnotify-${name}" {
                    Unit = {
                      Description = "Git fsnotify service ${name}";
                      After = [ "network-online.target" ];
                      Wants = [ "network-online.target" ];
                    };

                    Install = {
                      WantedBy = [ "default.target" ];
                    };

                    Service = {
                      ExecStart = "${git-fsnotify}/bin/git-fsnotify --path ${lib.escapeShellArg (toString service.path)} --log-level ${lib.escapeShellArg service.logLevel}";
                      Restart = "on-failure";
                    };
                  })
                enabledServices;
            })

            (lib.mkIf pkgs.stdenv.isDarwin {
              launchd.agents = lib.mapAttrs'
                (name: service:
                  lib.nameValuePair "git-fsnotify-${name}" {
                    enable = true;

                    config = {
                      Label = "org.nix-community.home.git-fsnotify-${name}";
                      ProgramArguments = [
                        "${git-fsnotify}/bin/git-fsnotify"
                        "--path"
                        (toString service.path)
                        "--log-level"
                        service.logLevel
                      ];
                      KeepAlive = true;
                      RunAtLoad = true;
                    };
                  })
                enabledServices;
            })
          ]);
        };
    in
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          craneLib = crane.mkLib pkgs;
          git-fsnotify = craneLib.buildPackage {
            src = craneLib.cleanCargoSource ./.;
            nativeBuildInputs = [ pkgs.pkg-config pkgs.openssl ];
          };
        in
        {
          devShells.default = pkgs.mkShell {
            packages = [ pkgs.pkg-config pkgs.openssl ];
          };

          packages.default = git-fsnotify;
        }) // {
      homeManagerModules.default = homeManagerModule;
      homeManagerModules.git-fsnotify = homeManagerModule;
      nixosModules.default = { ... }: {
        home-manager.sharedModules = [ homeManagerModule ];
      };
      darwinModules.default = { ... }: {
        home-manager.sharedModules = [ homeManagerModule ];
      };
    };
}
