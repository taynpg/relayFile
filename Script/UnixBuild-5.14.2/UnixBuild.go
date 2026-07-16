//go:build !windows
// +build !windows

package main

import (
	"fmt"
	"os"
	"path/filepath"

	"relayfile/scripts/Common"
)

func main() {
	useGUI := Common.AskYesNo("是否构建 GUI 版本", false)

	root := Common.ProjectRoot()
	buildDir := filepath.Join(root, "build-release-unix")
	config := "Release"

	fmt.Println("Project root:", root)
	fmt.Println("Build dir:  ", buildDir)
	fmt.Println("GUI:        ", useGUI)

	Common.RemoveBuildDir(buildDir)

	homeDir, err := os.UserHomeDir()
	if err != nil {
		panic(fmt.Sprintf("Failed to get home directory: %v", err))
	}

	qtPrefix := filepath.Join(homeDir, "Qt5.14.2", "5.14.2", "gcc_64")

	cmakeArgs := []string{
		"-B", buildDir,
		"-S", root,
		"-DCMAKE_BUILD_TYPE=" + config,
		"-DQT_DEFAULT_MAJOR_VERSION=5",
		"-DCMAKE_PREFIX_PATH=" + qtPrefix,
	}

	if useGUI {
		cmakeArgs = append(cmakeArgs, "-DQAPPLICATION_CLASS=QApplication")
		cmakeArgs = append(cmakeArgs, "-DRF_USE_GUI=ON")
	} else {
		cmakeArgs = append(cmakeArgs, "-DRF_USE_GUI=OFF")
	}

	Common.Run(root, "cmake", cmakeArgs...)
	Common.Run(root, "cmake", "--build", buildDir, "--config", config)

	fmt.Println("\n✓ Build finished successfully")
}
