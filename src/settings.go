package main

import (
	"os"
	"path/filepath"
	"strings"
)

// 记住上次打开的目录：持久化到注册表 HKCU\Software\gomd\LastFolder（REG_SZ）。
// 测试可用环境变量 GOMD_REG_KEY 把子键重定向到 Software\gomd_test，避免污染真实配置。

const regValueLastFolder = "LastFolder"

// regSubKey 返回 HKCU 下的配置子键。
func regSubKey() string {
	if k := os.Getenv("GOMD_REG_KEY"); k != "" {
		return k
	}
	return `Software\gomd`
}

// loadLastFolder 读取上次打开的目录；记录不存在或目录已失效时返回 ""。
func loadLastFolder() string {
	v, err := regGetString(HKEY_CURRENT_USER, regSubKey(), regValueLastFolder)
	if err != nil {
		return ""
	}
	f := strings.TrimSpace(v)
	if f == "" {
		return ""
	}
	if st, err := os.Stat(f); err != nil || !st.IsDir() {
		return ""
	}
	return f
}

// saveLastFolder 把目录写入注册表（空值或无效目录直接忽略）。
func saveLastFolder(dir string) {
	if dir == "" {
		return
	}
	if st, err := os.Stat(dir); err != nil || !st.IsDir() {
		return
	}
	_ = regSetString(HKEY_CURRENT_USER, regSubKey(), regValueLastFolder, dir)
}

// deleteLastFolder 清除记录（测试用）。
func deleteLastFolder() {
	regDeleteValue(HKEY_CURRENT_USER, regSubKey(), regValueLastFolder)
}

// rememberFolder 更新内存中的当前目录并落盘。
// 只在“用户主动打开文件/文件夹/切换目录”时调用，启动时的自动演示加载不要调用，
// 否则每次启动都会把记录覆盖成 exe 所在目录。
func rememberFolder(dir string) {
	if dir == "" {
		return
	}
	dlgFolder = dir
	saveLastFolder(dir)
}

// initialFolder 决定对话框初始目录：内存中的当前目录 > 上次记录 > exe 所在目录。
func initialFolder() string {
	if dlgFolder != "" {
		if st, err := os.Stat(dlgFolder); err == nil && st.IsDir() {
			return dlgFolder
		}
	}
	if f := loadLastFolder(); f != "" {
		return f
	}
	if exe, err := os.Executable(); err == nil {
		return filepath.Dir(exe)
	}
	return "."
}
