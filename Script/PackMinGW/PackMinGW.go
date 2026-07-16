package main

import (
	"fmt"
	"os"
	"path/filepath"

	"relayfile/scripts/Common"
)

const (
	QtDeploy = `C:\Qt\6.10.3\mingw_64\bin\windeployqt.exe`
	Config   = `Release`
	BuildDev = `build-dev-mingw`
)

func main() {
	isRelease := Common.AskYesNo("是否构建 release 版本", false)

	root := Common.ProjectRoot()

	buildName := BuildDev
	if isRelease {
		buildName = "build-release-mingw"
	}

	buildDir := filepath.Join(root, buildName)
	binDir := filepath.Join(buildDir, "bin", Config)
	versionH := filepath.Join(buildDir, "relayFileVersion.h")

	fmt.Println("Project root:", root)
	fmt.Println("Build dir:   ", buildDir)
	fmt.Println("Mode:        ", map[bool]string{true: "Release", false: "Dev"}[isRelease])

	Common.RemoveBuildDir(buildDir)

	cmakeArgs := []string{
		"-B", buildDir,
		"-S", root,
		"-DCMAKE_BUILD_TYPE=Release",
		"-DRF_USE_MINGW=ON",
		"-DQT_DEFAULT_MAJOR_VERSION=6",
		"-DQAPPLICATION_CLASS=QApplication",
		"-G", "MinGW Makefiles",
	}

	if isRelease {
		cmakeArgs = append(cmakeArgs, "-DRELEASE_MARK=ON")
	}

	path := os.Getenv("PATH")
	mingw := `C:\Qt\Tools\mingw1310_64\bin`
	opts := Common.CmdOptions{Dir: root}
	if !Common.ContainsPath(path, mingw) {
		opts.Env = append(os.Environ(), "PATH="+path+";"+mingw)
	}

	Common.RunWithOptions(opts, "cmake", cmakeArgs...)
	Common.Run(root, "cmake", "--build", buildDir, "--config", Config)

	guiExe := filepath.Join(binDir, "relayFileGui.exe")
	Common.Run(root, QtDeploy, guiExe)

	fmt.Println("\nReading version info:", versionH)
	v := Common.ParseVersionH(versionH)
	fmt.Printf("  Commit: %s\n  Version: %s\n  Dev:     %s\n",
		v.Commit, v.Num, v.Dev)

	zipName := Common.BuildZipName(v, "mingw")
	zipPath := filepath.Join(binDir, zipName)
	fmt.Println("  Zip:    ", zipName)

	exes := []string{
		filepath.Join(binDir, "relayFileClient.exe"),
		filepath.Join(binDir, "relayFileGui.exe"),
		filepath.Join(binDir, "relayFileServer.exe"),
	}

	if err := Common.CreateZip(zipPath, exes, v); err != nil {
		panic(err)
	}
}
