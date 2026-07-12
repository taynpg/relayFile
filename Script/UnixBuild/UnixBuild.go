//go:build !windows
// +build !windows

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

func projectRoot() string {
	_, file, _, _ := runtime.Caller(0)
	root := filepath.Join(filepath.Dir(file), "..", "..")
	root, err := filepath.Abs(root)
	if err != nil {
		panic(err)
	}
	return root
}

func run(dir, name string, args ...string) {
	cmd := exec.Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Dir = dir

	fmt.Printf("-> %s %v (dir=%s)\n", name, args, dir)
	if err := cmd.Run(); err != nil {
		panic(err)
	}
}

func askUseGUI() bool {
	fmt.Print("是否构建 GUI 版本? (y/N): ")
	reader := bufio.NewReader(os.Stdin)
	line, _ := reader.ReadString('\n')
	line = strings.TrimSpace(line)
	return strings.EqualFold(line, "y")
}

func main() {
	useGUI := askUseGUI()

	root := projectRoot()
	buildDir := filepath.Join(root, "build-release-unix")
	config := "Release"

	fmt.Println("Project root:", root)
	fmt.Println("Build dir:  ", buildDir)
	fmt.Println("GUI:        ", useGUI)

	if _, err := os.Stat(buildDir); err == nil {
		fmt.Println("Removing existing build directory:", buildDir)
		if err := os.RemoveAll(buildDir); err != nil {
			panic(fmt.Sprintf("Failed to remove build directory: %v", err))
		}
		fmt.Println("✓ Build directory removed")
	}

	cmakeArgs := []string{
		"-B", buildDir,
		"-S", root,
		"-DCMAKE_BUILD_TYPE=" + config,
		"-DQT_DEFAULT_MAJOR_VERSION=6",
	}

	if useGUI {
		cmakeArgs = append(cmakeArgs, "-DQAPPLICATION_CLASS=QApplication")
		cmakeArgs = append(cmakeArgs, "-DRF_USE_GUI=ON")
	} else {
		cmakeArgs = append(cmakeArgs, "-DRF_USE_GUI=OFF")
	}

	run(root, "cmake", cmakeArgs...)
	run(root, "cmake", "--build", buildDir, "--config", config)

	fmt.Println("\n✓ Build finished successfully")
}
