{
  description = "git-watch";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    crane.url = "github:ipetkov/crane";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, crane }:
    let
      homeManagerModule = { config, lib, pkgs, ... }:
        let
          cfg = config.services.git-watch;
          enabledServices = lib.filterAttrs (_: service: service.enable) cfg;
          git-watch = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        in
        {
          options = {
            services.git-watch = lib.mkOption {
              type = lib.types.attrsOf (lib.types.submodule {
                options = {
                  enable = lib.mkEnableOption "git-watch";

                  path = lib.mkOption {
                    type = lib.types.either lib.types.path lib.types.str;
                    default = lib.mkHomeDirPath "git-watch";
                    description = "Path to watch";
                  };

                  logLevel = lib.mkOption {
                    type = lib.types.enum [ "trace" "debug" "info" "warn" "error" ];
                    default = "info";
                    description = "Log level";
                  };

                  interval = lib.mkOption {
                    type = lib.types.ints.positive;
                    default = 60;
                    description = "Sync interval in seconds";
                  };

                  sshAuthSock = lib.mkOption {
                    type = lib.types.nullOr lib.types.str;
                    default = config.home.sessionVariables.SSH_AUTH_SOCK or null;
                    description = "SSH agent socket path to pass to git-watch. Defaults to home.sessionVariables.SSH_AUTH_SOCK when set.";
                  };
                };
              });
              default = { };
              description = "git-watch service instances.";
            };
          };

          config = lib.mkIf (enabledServices != { }) (lib.mkMerge [
            {
              home.packages = [ git-watch ];
            }

            (lib.mkIf pkgs.stdenv.isLinux {
              systemd.user.services = lib.mapAttrs'
                (name: service:
                  lib.nameValuePair "git-watch-${name}" {
                    Unit = {
                      Description = "Git fsnotify service ${name}";
                      After = [ "network-online.target" ];
                      Wants = [ "network-online.target" ];
                    };

                    Install = {
                      WantedBy = [ "default.target" ];
                    };

                    Service = {
                      ExecStart = "${git-watch}/bin/git-watch --path ${lib.escapeShellArg (toString service.path)} --log-level ${lib.escapeShellArg service.logLevel} --interval ${toString service.interval}";
                      Restart = "on-failure";
                    } // lib.optionalAttrs (service.sshAuthSock != null) {
                      Environment = "SSH_AUTH_SOCK=${service.sshAuthSock}";
                    };
                  })
                enabledServices;
            })

            (lib.mkIf pkgs.stdenv.isDarwin {
              launchd.agents = lib.mapAttrs'
                (name: service:
                  lib.nameValuePair "git-watch-${name}" {
                    enable = true;

                    config = {
                      Label = "org.nix-community.home.git-watch-${name}";
                      ProgramArguments = [
                        "${git-watch}/bin/git-watch"
                        "--path"
                        (toString service.path)
                        "--log-level"
                        service.logLevel
                        "--interval"
                        (toString service.interval)
                      ];
                      KeepAlive = true;
                      RunAtLoad = true;
                    } // lib.optionalAttrs (service.sshAuthSock != null) {
                      EnvironmentVariables = {
                        SSH_AUTH_SOCK = service.sshAuthSock;
                      };
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
          git-watch = craneLib.buildPackage {
            src = craneLib.cleanCargoSource ./.;
            nativeBuildInputs = [ pkgs.pkg-config pkgs.openssl ];
          };
        in
        {
          devShells.default = pkgs.mkShell {
            packages = [ pkgs.pkg-config pkgs.openssl ];
          };

          packages.default = git-watch;
        }) // {
      homeManagerModules.default = homeManagerModule;
      homeManagerModules.git-watch = homeManagerModule;
      nixosModules.default = { ... }: {
        home-manager.sharedModules = [ homeManagerModule ];
      };
      darwinModules.default = { ... }: {
        home-manager.sharedModules = [ homeManagerModule ];
      };
    };
}
