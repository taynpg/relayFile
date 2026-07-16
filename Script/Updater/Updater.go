package main

import (
	"archive/zip"
	"bufio"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"relayfile/scripts/Common"
)

const (
	Owner = "taynpg"
	Repo  = "relayFile"

	MainExe = "relayFileGui.exe"
	Exes    = "relayFileClient.exe|relayFileGui.exe|relayFileServer.exe"
)

func pause() {
	fmt.Println("\n按 Enter 退出...")
	bufio.NewReader(os.Stdin).ReadString('\n')
}

func initProxy() {
	defaultProxy := "http://127.0.0.1:7897"

	if !Common.AskYesNo("是否使用代理", false) {
		fmt.Println("不使用代理")
		return
	}

	proxy := defaultProxy
	if !Common.AskYesNo(fmt.Sprintf("是否使用默认代理 (%s)", defaultProxy), true) {
		fmt.Print("请输入代理地址: ")
		reader := bufio.NewReader(os.Stdin)
		proxy, _ = reader.ReadString('\n')
		proxy = strings.TrimSpace(proxy)
	}

	if !strings.HasPrefix(proxy, "http://") && !strings.HasPrefix(proxy, "https://") {
		proxy = "http://" + proxy
	}

	os.Setenv("HTTP_PROXY", proxy)
	os.Setenv("HTTPS_PROXY", proxy)

	fmt.Println("✓ 代理已设置:", proxy)
}

func download(url, dst string) error {
	fmt.Println("下载:", url)

	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return fmt.Errorf("下载失败: %s", resp.Status)
	}

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, resp.Body)
	if err != nil {
		return err
	}

	fmt.Println("✓ 下载完成")
	return nil
}

func fetchUpdateList(tag string) ([]string, error) {
	url := fmt.Sprintf(
		"https://github.com/%s/%s/releases/download/%s/update_list.txt",
		Owner, Repo, tag,
	)

	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("update_list.txt 不存在或无权限")
	}

	body, _ := io.ReadAll(resp.Body)

	var list []string
	for _, line := range strings.Split(strings.TrimSpace(string(body)), "\n") {
		line = strings.TrimSpace(line)
		if line != "" {
			list = append(list, line)
		}
	}

	if len(list) == 0 {
		return nil, fmt.Errorf("update_list.txt 为空")
	}
	return list, nil
}

func extractZip(zipPath, dest string) error {
	fmt.Printf("zip路径：%s\n", zipPath)
	fmt.Printf("目标路径：%s\n", dest)

	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return err
	}
	defer r.Close()

	exeList := strings.Split(Exes, "|")

	for _, f := range r.File {
		for _, name := range exeList {
			if f.Name == name {
				rc, _ := f.Open()
				defer rc.Close()

				target := filepath.Join(dest, name)
				_ = os.Remove(target)

				out, err := os.Create(target)
				if err != nil {
					return fmt.Errorf("无法写入 %s: %v", name, err)
				}
				io.Copy(out, rc)
				out.Close()
				fmt.Println("✓ 更新", name)
			}
		}
	}
	return nil
}

func killMain() {
	Common.Run("", "taskkill", "/F", "/IM", MainExe)
}

func startMain(exePath string) {
	cmd := exec.Command(exePath)
	cmd.Dir = filepath.Dir(exePath)
	_ = cmd.Start()
}

func main() {
	defer pause()

	fmt.Println("=== relayFile Updater ===")

	initProxy()

	reader := bufio.NewReader(os.Stdin)

	fmt.Print("请输入 Release Tag（如 v0.2）: ")
	tag, _ := reader.ReadString('\n')
	tag = strings.TrimSpace(tag)

	files, err := fetchUpdateList(tag)
	if err != nil {
		fmt.Println("❌", err)
		return
	}

	fmt.Println("\n可选的更新包：")
	for i, f := range files {
		fmt.Printf(" [%d] %s\n", i, f)
	}

	fmt.Print("请选择编号：")
	var idx int
	_, err = fmt.Scanln(&idx)
	if err != nil || idx < 0 || idx >= len(files) {
		fmt.Println("❌ 无效的选择")
		return
	}

	zipName := files[idx]

	exeDir, err := os.Executable()
	if err != nil {
		fmt.Println("❌ 获取路径失败:", err)
		return
	}
	exeDir = filepath.Dir(exeDir)

	zipPath := filepath.Join(exeDir, "__update__.zip")
	dlURL := fmt.Sprintf(
		"https://github.com/%s/%s/releases/download/%s/%s",
		Owner, Repo, tag, zipName,
	)

	if err := download(dlURL, zipPath); err != nil {
		fmt.Println("❌", err)
		return
	}

	killMain()

	if err := extractZip(zipPath, exeDir); err != nil {
		fmt.Println("❌ 解压失败:", err)
		return
	}

	os.Remove(zipPath)

	startMain(filepath.Join(exeDir, MainExe))
	fmt.Println("✓ 更新完成")
}
