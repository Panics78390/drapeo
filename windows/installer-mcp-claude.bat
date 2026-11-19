@echo off
rem Double-cliquer : ajoute Drapeo aux serveurs MCP de Claude Desktop.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer-mcp-claude.ps1"
pause
