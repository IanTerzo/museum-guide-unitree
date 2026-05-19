package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
)

//go:embed static/index.html
var indexHTML []byte

type Config struct {
	Addr   string `json:"addr"`
	Server string `json:"server"`
}

var cfg Config

func loadConfig(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, &cfg)
}

func proxy(w http.ResponseWriter, r *http.Request, target string) {
	req, err := http.NewRequest(r.Method, target, r.Body)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	for k, v := range r.Header {
		req.Header[k] = v
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		http.Error(w, "upstream error: "+err.Error(), http.StatusBadGateway)
		return
	}
	defer resp.Body.Close()

	for k, v := range resp.Header {
		w.Header()[k] = v
	}
	w.WriteHeader(resp.StatusCode)
	io.Copy(w, resp.Body)
}

func main() {
	configPath := flag.String("config", "config.json", "path to config file")
	flag.Parse()

	if err := loadConfig(*configPath); err != nil {
		log.Fatalf("failed to load config: %v", err)
	}

	server := strings.TrimRight(cfg.Server, "/")
	addr := cfg.Addr
	if addr == "" {
		addr = ":62324"
	}

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(indexHTML)
	})

	http.HandleFunc("/sounds", func(w http.ResponseWriter, r *http.Request) {
		proxy(w, r, server+"/sounds")
	})

	http.HandleFunc("/sounds/", func(w http.ResponseWriter, r *http.Request) {
		suffix := strings.TrimPrefix(r.URL.Path, "/sounds")
		proxy(w, r, server+"/sounds"+suffix)
	})

	http.HandleFunc("/play", func(w http.ResponseWriter, r *http.Request) {
		proxy(w, r, server+"/play")
	})

	http.HandleFunc("/upload", func(w http.ResponseWriter, r *http.Request) {
		proxy(w, r, server+"/upload")
	})

	log.Printf("museumTTS UI listening on %s → %s", addr, server)
	log.Fatal(http.ListenAndServe(addr, nil))
}
