package Common

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

func ProjectRoot() string {
	_, file, _, _ := runtime.Caller(0)
	root := filepath.Join(filepath.Dir(file), "..", "..")
	root, err := filepath.Abs(root)
	if err != nil {
		panic(err)
	}
	return root
}

type CmdOptions struct {
	Dir    string
	Env    []string
	Stdout io.Writer
	Stderr io.Writer
}

func Run(dir, name string, args ...string) {
	RunWithOptions(CmdOptions{Dir: dir}, name, args...)
}

func RunWithOptions(opts CmdOptions, name string, args ...string) {
	cmd := exec.Command(name, args...)

	if opts.Stdout != nil {
		cmd.Stdout = opts.Stdout
	} else {
		cmd.Stdout = os.Stdout
	}

	if opts.Stderr != nil {
		cmd.Stderr = opts.Stderr
	} else {
		cmd.Stderr = os.Stderr
	}

	if opts.Dir != "" {
		cmd.Dir = opts.Dir
	}

	if len(opts.Env) > 0 {
		cmd.Env = opts.Env
	}

	fmt.Printf("-> %s %v (dir=%s)\n", name, args, cmd.Dir)
	if err := cmd.Run(); err != nil {
		panic(err)
	}
}

func ContainsPath(path, target string) bool {
	return len(path) >= len(target) &&
		(path == target || path[:len(target)] == target ||
			strings.Contains(path, target+";") ||
			strings.Contains(path, ";"+target))
}

func AskYesNo(prompt string, defaultValue bool) bool {
	defaultStr := "N"
	if defaultValue {
		defaultStr = "Y"
	}
	fmt.Printf("%s (%s/n): ", prompt, defaultStr)

	reader := bufio.NewReader(os.Stdin)
	line, _ := reader.ReadString('\n')
	line = strings.TrimSpace(strings.ToLower(line))

	if line == "" {
		return defaultValue
	}
	return line == "y" || line == "yes"
}

type VersionInfo struct {
	Commit string
	Branch string
	Num    string
	URL    string
	Dev    string
}

func ParseVersionH(path string) VersionInfo {
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

func BuildZipName(v VersionInfo, suffix string) string {
	date := time.Now().Format("20060102")
	if v.Dev == "dev" {
		return fmt.Sprintf("relayFile-%s-v%s-%s-%s-%s.zip",
			v.Commit, v.Num, v.Dev, date, suffix)
	} else {
		return fmt.Sprintf("relayFile-%s-v%s-%s-%s.zip",
			v.Commit, v.Num, v.Dev, suffix)
	}
}

func RemoveBuildDir(buildDir string) {
	if _, err := os.Stat(buildDir); err == nil {
		fmt.Println("Removing existing build directory:", buildDir)
		if err := os.RemoveAll(buildDir); err != nil {
			panic(fmt.Sprintf("Failed to remove build directory: %v", err))
		}
		fmt.Println("✓ Build directory removed")
	}
}

func CreateZip(zipPath string, files []string, root string, v VersionInfo) error {
	zf, err := os.Create(zipPath)
	if err != nil {
		return err
	}
	defer zf.Close()

	zw := zip.NewWriter(zf)
	defer zw.Close()

	vw, _ := zw.Create("version.txt")
	fmt.Fprintf(vw, "COMMIT=%s\nVERSION=%s\nDEV=%s\n",
		v.Commit, v.Num, v.Dev)

	// 添加项目自身的 LICENSE 文件
	licensePath := filepath.Join(root, "LICENSE")
	if f, err := os.Open(licensePath); err == nil {
		w, _ := zw.Create("LICENSE")
		io.Copy(w, f)
		f.Close()
		fmt.Println("  ✓ LICENSE")
	}

	// 添加 licenses/ 目录下的第三方协议文件（如 Qt 的 LGPL）
	licensesDir := filepath.Join(root, "licenses")
	if entries, err := os.ReadDir(licensesDir); err == nil {
		for _, entry := range entries {
			if entry.IsDir() {
				continue
			}
			src := filepath.Join(licensesDir, entry.Name())
			f, err := os.Open(src)
			if err != nil {
				continue
			}
			w, _ := zw.Create(filepath.Join("licenses", entry.Name()))
			io.Copy(w, f)
			f.Close()
			fmt.Println("  ✓ licenses/" + entry.Name())
		}
	}

	for _, exe := range files {
		f, err := os.Open(exe)
		if err != nil {
			fmt.Println("  跳过", filepath.Base(exe), ":", err)
			continue
		}
		defer f.Close()

		w, _ := zw.Create(filepath.Base(exe))
		io.Copy(w, f)
		fmt.Println("  ✓", filepath.Base(exe))
	}

	fmt.Println("\n✓ Package created:", zipPath)
	return nil
}
