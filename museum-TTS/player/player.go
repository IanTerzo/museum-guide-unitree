package main

import (
	"os/exec"
)

func playFile(path string) error {
	return exec.Command("paplay", "--device=g1_speaker", path).Run()
}
