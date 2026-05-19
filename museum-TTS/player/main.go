package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"unicode"
)

//go:embed static/index.html
var indexHTML []byte

type Sound struct {
	Name string `json:"name"`
	Path string `json:"path"`
}

type Config struct {
	Addr   string  `json:"addr"`
	Sounds []Sound `json:"sounds"`
}

var (
	cfg       Config
	cfgPath   string
	uploadDir string
	cfgMu     sync.RWMutex
	playLock  sync.Mutex
)

func resolvePath(base, p string) string {
	if strings.HasPrefix(p, "~/") {
		home, err := os.UserHomeDir()
		if err == nil {
			p = filepath.Join(home, p[2:])
		}
	}
	if filepath.IsAbs(p) {
		return p
	}
	return filepath.Join(base, p)
}

func loadConfig(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return err
	}
	base := filepath.Dir(path)
	for i := range cfg.Sounds {
		cfg.Sounds[i].Path = resolvePath(base, cfg.Sounds[i].Path)
	}
	return nil
}

func saveConfig() error {
	cfgMu.RLock()
	data, err := json.MarshalIndent(cfg, "", "  ")
	cfgMu.RUnlock()
	if err != nil {
		return err
	}
	tmp := cfgPath + ".tmp"
	if err := os.WriteFile(tmp, data, 0644); err != nil {
		return err
	}
	return os.Rename(tmp, cfgPath)
}

func sanitizeFilename(s string) string {
	var b strings.Builder
	for _, r := range s {
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '-' || r == '_' || r == '.' {
			b.WriteRune(r)
		} else if r == ' ' {
			b.WriteRune('_')
		}
	}
	result := b.String()
	if result == "" {
		result = "sound"
	}
	return result
}

func playHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	file := r.FormValue("file")
	if file == "" {
		http.Error(w, "missing 'file' parameter", http.StatusBadRequest)
		return
	}

	if _, err := os.Stat(file); err != nil {
		http.Error(w, "file not found: "+file, http.StatusNotFound)
		return
	}

	playLock.Lock()
	defer playLock.Unlock()

	if err := playFile(file); err != nil {
		http.Error(w, "playback error: "+err.Error(), http.StatusInternalServerError)
		return
	}

	fmt.Fprintln(w, "ok")
}

func soundsHandler(w http.ResponseWriter, r *http.Request) {
	cfgMu.RLock()
	sounds := make([]Sound, len(cfg.Sounds))
	copy(sounds, cfg.Sounds)
	cfgMu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(sounds)
}

func uploadHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := r.ParseMultipartForm(64 << 20); err != nil {
		http.Error(w, "bad request: "+err.Error(), http.StatusBadRequest)
		return
	}

	name := strings.TrimSpace(r.FormValue("name"))
	if name == "" {
		http.Error(w, "missing 'name' field", http.StatusBadRequest)
		return
	}

	file, header, err := r.FormFile("file")
	if err != nil {
		http.Error(w, "missing 'file' field: "+err.Error(), http.StatusBadRequest)
		return
	}
	defer file.Close()

	ext := strings.ToLower(filepath.Ext(header.Filename))
	if ext != ".wav" && ext != ".mp3" {
		http.Error(w, "only .wav and .mp3 files are supported", http.StatusBadRequest)
		return
	}

	if err := os.MkdirAll(uploadDir, 0755); err != nil {
		http.Error(w, "failed to create upload dir: "+err.Error(), http.StatusInternalServerError)
		return
	}

	destPath := filepath.Join(uploadDir, sanitizeFilename(name)+".wav")


	out, err := os.Create(destPath)
	if err != nil {
		http.Error(w, "failed to save file: "+err.Error(), http.StatusInternalServerError)
		return
	}
	if _, err := io.Copy(out, file); err != nil {
		out.Close()
		os.Remove(destPath)
		http.Error(w, "failed to write file: "+err.Error(), http.StatusInternalServerError)
		return
	}
	out.Close()

	cfgMu.Lock()
	found := false
	for i, s := range cfg.Sounds {
		if strings.EqualFold(s.Name, name) {
			cfg.Sounds[i].Path = destPath
			found = true
			break
		}
	}
	if !found {
		cfg.Sounds = append(cfg.Sounds, Sound{Name: name, Path: destPath})
	}
	cfgMu.Unlock()

	if err := saveConfig(); err != nil {
		log.Printf("warning: failed to persist config: %v", err)
	}

	log.Printf("uploaded sound %q -> %s", name, destPath)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(Sound{Name: name, Path: destPath})
}

func deleteHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	name := strings.TrimPrefix(r.URL.Path, "/sounds/")
	name = strings.TrimSpace(name)
	if name == "" {
		http.Error(w, "missing sound name", http.StatusBadRequest)
		return
	}

	cfgMu.Lock()
	idx := -1
	for i, s := range cfg.Sounds {
		if strings.EqualFold(s.Name, name) {
			idx = i
			break
		}
	}
	if idx == -1 {
		cfgMu.Unlock()
		http.Error(w, "sound not found", http.StatusNotFound)
		return
	}
	cfg.Sounds = append(cfg.Sounds[:idx], cfg.Sounds[idx+1:]...)
	cfgMu.Unlock()

	if err := saveConfig(); err != nil {
		log.Printf("warning: failed to persist config: %v", err)
	}

	log.Printf("deleted sound %q", name)
	fmt.Fprintln(w, "ok")
}

func logged(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		log.Printf("%s %s %s", r.RemoteAddr, r.Method, r.URL)
		h(w, r)
	}
}

func indexHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(indexHTML)
}

func main() {
	configPath := flag.String("config", "config.json", "path to config file")
	flag.Parse()

	abs, err := filepath.Abs(*configPath)
	if err != nil {
		log.Fatalf("failed to resolve config path: %v", err)
	}
	cfgPath = abs

	if err := loadConfig(cfgPath); err != nil {
		log.Fatalf("failed to load config: %v", err)
	}

	uploadDir = filepath.Join(filepath.Dir(cfgPath), "sounds")

	addr := cfg.Addr
	if addr == "" {
		addr = ":8080"
	}

	http.HandleFunc("/", logged(indexHandler))
	http.HandleFunc("/sounds", logged(soundsHandler))
	http.HandleFunc("/sounds/", logged(deleteHandler))
	http.HandleFunc("/play", logged(playHandler))
	http.HandleFunc("/upload", logged(uploadHandler))

	log.Printf("museumTTS listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}
