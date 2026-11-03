"""
GitHub Copilot authentication.
The OAuth token (gho_...) stored by OpenCode can be used directly
as a Bearer token for the Copilot Chat API.
"""

import json
from pathlib import Path

OPENCODE_AUTH = Path.home() / ".local/share/opencode/auth.json"
COPILOT_API_BASE = "https://api.githubcopilot.com"


def get_copilot_token() -> str:
    """Return the GitHub OAuth token stored by OpenCode."""
    with open(OPENCODE_AUTH) as f:
        data = json.load(f)
    token = data["github-copilot"]["access"]
    if not token:
        raise RuntimeError("No github-copilot access token in OpenCode auth.json")
    return token


def copilot_headers() -> dict:
    """Return HTTP headers for Copilot API requests."""
    return {
        "Authorization": f"Bearer {get_copilot_token()}",
        "Content-Type": "application/json",
        "Copilot-Integration-Id": "vscode-chat",
        "Editor-Version": "vscode/1.85.0",
        "User-Agent": "rover-backend/1.0",
    }
