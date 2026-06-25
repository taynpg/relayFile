package main

import (
	"archive/zip"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

const (
	Owner   = "taynpg"
	Repo    = "relayFile"
	MainExe = "relayFileGui.exe"
	Exes    = "relayFileClient.exe|relayFileGui.exe|relayFileServer.exe"
)

type Asset struct {
	Name string `json:"name"`
	URL  string `json:"browser_download_url"`
}

type Release struct {
	TagName string  `json:"tag_name"`
	Assets  []Asset `json:"assets"`
}

type LocalVersion struct {
	Commit string
	Num    string // 0.2
	Dev    string // dev / release
}

func parseLocalVersion(output string) LocalVersion {
	output = strings.TrimSpace(output)
	parts := strings.Split(output, "-")
	if len(parts) < 3 {
		return LocalVersion{}
	}
	return LocalVersion{
		Commit: parts[0],
		Num:    strings.TrimPrefix(parts[1], "v"),
		Dev:    parts[2],
	}
}

// 解析 zip 文件名：relayFile-7c262c1-v0.2-dev-20260312.zip
func parseZipName(name string) (commit, version, dev string) {
	name = strings.TrimSuffix(name, ".zip")
	parts := strings.Split(name, "-")
	if len(parts) < 5 {
		return
	}
	commit = parts[1]
	version = strings.TrimPrefix(parts[2], "v")
	dev = parts[3]
	return
}

func versionGT(a, b string) bool {
	majA, minA := splitVersion(a)
	majB, minB := splitVersion(b)
	return majA > majB || (majA == majB && minA > minB)
}

func splitVersion(v string) (int, int) {
	parts := strings.Split(v, ".")
	maj, _ := strconv.Atoi(parts[0])
	min := 0
	if len(parts) > 1 {
		min, _ = strconv.Atoi(parts[1])
	}
	return maj, min
}

func getLatestRelease() Release {
	url := fmt.Sprintf("https://api.github.com/repos/%s/%s/releases/latest", Owner, Repo)
	req, _ := http.NewRequest("GET", url, nil)
	req.Header.Set("User-Agent", "relayFile-updater")

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		panic("获取 release 失败: " + err.Error())
	}
	defer resp.Body.Close()

	var r Release
	if err := json.NewDecoder(resp.Body).Decode(&r); err != nil {
		panic("解析 release JSON 失败: " + err.Error())
	}
	return r
}

func selectUpdateAsset(local LocalVersion, assets []Asset) (*Asset, string) {
	var devCandidate *Asset

	for _, asset := range assets {
		commit, ver, dev := parseZipName(asset.Name)
		if ver == "" {
			continue
		}
		if dev == "release" && versionGT(ver, local.Num) {
			return &asset, fmt.Sprintf("发现更高版本 release v%s", ver)
		}
		if dev == "dev" &&
			ver == local.Num &&
			commit != local.Commit &&
			devCandidate == nil {
			devCandidate = &asset
		}
	}

	if devCandidate != nil {
		return devCandidate, "发现新 dev 构建"
	}

	return nil, "已是最新版本"
}

func download(url, dst string) {
	req, _ := http.NewRequest("GET", url, nil)
	req.Header.Set("User-Agent", "relayFile-updater")

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		panic("下载失败: " + err.Error())
	}
	defer resp.Body.Close()

	f, err := os.Create(dst)
	if err != nil {
		panic(err)
	}
	defer f.Close()

	fmt.Print("下载中...")
	_, err = io.Copy(f, resp.Body)
	if err != nil {
		panic(err)
	}
	fmt.Println(" ✓")
}

func extractZip(zipPath, destDir string) {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		panic(err)
	}
	defer r.Close()

	exeList := strings.Split(Exes, "|")

	for _, f := range r.File {
		for _, name := range exeList {
			if f.Name == name {
				rc, _ := f.Open()
				defer rc.Close()

				outPath := filepath.Join(destDir, name)
				_ = os.Remove(outPath)

				out, err := os.Create(outPath)
				if err != nil {
					panic("无法写入 " + name + ": " + err.Error())
				}
				io.Copy(out, rc)
				out.Close()
				fmt.Println("  ✓ 更新", name)
				break
			}
		}
	}
}

func killMain() {
	exec.Command("taskkill", "/F", "/IM", MainExe).Run()
}

func startMain(exePath string) {
	cmd := exec.Command(exePath)
	cmd.Dir = filepath.Dir(exePath)
	_ = cmd.Start()
}

func main() {
	fmt.Println("=== relayFile Updater ===")

	exeDir, err := os.Executable()
	if err != nil {
		panic(err)
	}
	exeDir = filepath.Dir(exeDir)

	mainExe := filepath.Join(exeDir, MainExe)

	out, err := exec.Command(mainExe, "--version").Output()
	if err != nil {
		fmt.Println("无法获取本地版本，可能是首次运行")
		return
	}
	local := parseLocalVersion(string(out))
	fmt.Printf("本地版本: %s (commit %s)\n", local.Num, local.Commit)

	rel := getLatestRelease()
	fmt.Printf("Assets count: %d\n", len(rel.Assets))
	fmt.Println("Latest release:", rel.TagName)

	asset, reason := selectUpdateAsset(local, rel.Assets)
	if asset == nil {
		fmt.Println(reason)
		return
	}

	fmt.Println("准备更新:", asset.Name)
	fmt.Println("原因:", reason)

	zipPath := filepath.Join(exeDir, "__update__.zip")
	download(asset.URL, zipPath)

	killMain()

	extractZip(zipPath, exeDir)

	_ = os.Remove(zipPath)

	startMain(mainExe)

	fmt.Println("✓ 更新完成，程序已重启")
}
