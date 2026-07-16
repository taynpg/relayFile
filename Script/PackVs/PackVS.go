package main

import (
	"fmt"
	"path/filepath"

	"relayfile/scripts/Common"
)

const (
	QtDeploy = `C:\Qt\6.10.3\msvc2022_64\bin\windeployqt.exe`
	Config   = `Release`
	BuildDev = `build-dev-msvc`
)

func main() {
	isRelease := Common.AskYesNo("是否构建 release 版本", false)

	root := Common.ProjectRoot()

	buildName := BuildDev
	if isRelease {
		buildName = "build-release-msvc"
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
		"-DQT_DEFAULT_MAJOR_VERSION=6",
		"-DQAPPLICATION_CLASS=QApplication",
		"-A", "x64",
	}

	if isRelease {
		cmakeArgs = append(cmakeArgs, "-DRELEASE_MARK=ON")
	}

	Common.Run(root, "cmake", cmakeArgs...)
	Common.Run(root, "cmake", "--build", buildDir, "--config", Config)

	guiExe := filepath.Join(binDir, "relayFileGui.exe")
	Common.Run(root, QtDeploy, guiExe)

	fmt.Println("\nReading version info:", versionH)
	v := Common.ParseVersionH(versionH)
	fmt.Printf("  Commit: %s\n  Version: %s\n  Dev:     %s\n",
		v.Commit, v.Num, v.Dev)

	zipName := Common.BuildZipName(v, "msvc")
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
