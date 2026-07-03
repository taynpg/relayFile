package main

import (
	"archive/zip"
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"time"
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

const (
	QtDeploy = `C:\Qt\6.10.3\mingw_64\bin\windeployqt.exe`
	Config   = `Release`
	BuildDev = `build-dev-mingw`
)

func containsPath(path, target string) bool {
	return len(path) >= len(target) &&
		(path == target || path[:len(target)] == target ||
			strings.Contains(path, target+";") ||
			strings.Contains(path, ";"+target))
}

func run(dir, name string, args ...string) {
	cmd := exec.Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Dir = dir

	path := os.Getenv("PATH")
	mingw := `C:\Qt\Tools\mingw1310_64\bin`
	if !containsPath(path, mingw) {
		cmd.Env = append(os.Environ(), "PATH="+path+";"+mingw)
	}

	fmt.Printf("-> %s %v (dir=%s)\n", name, args, dir)
	if err := cmd.Run(); err != nil {
		panic(err)
	}
}

func askRelease() bool {
	fmt.Print("是否构建 release 版本? (y/N): ")
	reader := bufio.NewReader(os.Stdin)
	line, _ := reader.ReadString('\n')
	line = strings.TrimSpace(line)
	return strings.EqualFold(line, "y")
}

type VersionInfo struct {
	Commit string
	Branch string
	Num    string
	URL    string
	Dev    string
}

func parseVersionH(path string) VersionInfo {
	f, err := os.Open(path)
	if err != nil {
		panic(fmt.Sprintf("无法打开 version.h: %s\n%v", path, err))
	}
	defer f.Close()

	var v VersionInfo
	scanner := bufio.NewScanner(f)
	re := regexp.MustCompile(`#define\s+(\w+)\s+"(.+)"`)

	for scanner.Scan() {
		line := scanner.Text()
		m := re.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		key, val := m[1], m[2]
		switch key {
		case "VERSION_GIT_COMMIT":
			v.Commit = val
		case "VERSION_GIT_BRANCH":
			v.Branch = val
		case "VERSION_NUM":
			v.Num = val
		case "VERSION_URL":
			v.URL = val
		case "VERSION_DEV":
			v.Dev = val
		}
	}

	if err := scanner.Err(); err != nil {
		panic(fmt.Sprintf("读取 version.h 失败: %v", err))
	}
	return v
}

func buildZipName(v VersionInfo) string {
	date := time.Now().Format("20060102")
	if v.Dev == "dev" {
		return fmt.Sprintf("relayFile-%s-v%s-%s-%s-mingw.zip",
			v.Commit, v.Num, v.Dev, date)
	} else {
		return fmt.Sprintf("relayFile-%s-v%s-%s-mingw.zip",
			v.Commit, v.Num, v.Dev)
	}
}

func main() {
	isRelease := askRelease()

	root := projectRoot()

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
		"-DCMAKE_BUILD_TYPE=Release",
		"-DRF_USE_MINGW=ON",
		"-G", "MinGW Makefiles",
	}

	if isRelease {
		cmakeArgs = append(cmakeArgs, "-DRELEASE_MARK=ON")
	}

	run(root, "cmake", cmakeArgs...)
	run(root, "cmake", "--build", buildDir, "--config", Config)

	guiExe := filepath.Join(binDir, "relayFileGui.exe")
	run(root, QtDeploy, guiExe)

	fmt.Println("\nReading version info:", versionH)
	v := parseVersionH(versionH)
	fmt.Printf("  Commit: %s\n  Version: %s\n  Dev:     %s\n",
		v.Commit, v.Num, v.Dev)

	zipName := buildZipName(v)
	zipPath := filepath.Join(binDir, zipName)
	fmt.Println("  Zip:    ", zipName)

	exes := []string{
		"relayFileClient.exe",
		"relayFileGui.exe",
		"relayFileServer.exe",
	}

	zf, err := os.Create(zipPath)
	if err != nil {
		panic(err)
	}
	defer zf.Close()

	zw := zip.NewWriter(zf)
	defer zw.Close()

	vw, _ := zw.Create("version.txt")
	fmt.Fprintf(vw, "COMMIT=%s\nVERSION=%s\nDEV=%s\n",
		v.Commit, v.Num, v.Dev)

	for _, exe := range exes {
		src := filepath.Join(binDir, exe)
		f, err := os.Open(src)
		if err != nil {
			fmt.Println("  跳过", exe, ":", err)
			continue
		}
		defer f.Close()

		w, _ := zw.Create(exe)
		io.Copy(w, f)
		fmt.Println("  ✓", exe)
	}

	fmt.Println("\n✓ Package created:", zipPath)
}
