#!/usr/bin/env python3
"""Simple HTTP server to view Claude projects data."""

import http.server
import json
import os
import urllib.parse
from pathlib import Path
from datetime import datetime

PROJECTS_DIR = Path.home() / '.claude' / 'projects'
VIEWER_DIR = Path(__file__).parent

class ProjectsHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(VIEWER_DIR), **kwargs)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path == '/api/projects':
            self.send_projects()
        elif parsed.path == '/api/session':
            query = urllib.parse.parse_qs(parsed.query)
            project = query.get('project', [''])[0]
            file = query.get('file', [''])[0]
            self.send_session(project, file)
        elif parsed.path == '/' or parsed.path == '':
            self.path = '/viewer.html'
            super().do_GET()
        else:
            super().do_GET()

    def send_projects(self):
        projects = []
        for item in sorted(PROJECTS_DIR.iterdir()):
            if item.is_dir() and item.name.startswith('-'):
                sessions = []
                for f in sorted(item.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True):
                    if f.suffix == '.jsonl':
                        stat = f.stat()
                        sessions.append({
                            'file': f.name,
                            'date': datetime.fromtimestamp(stat.st_mtime).strftime('%Y-%m-%d %H:%M'),
                            'size': stat.st_size
                        })

                # Format project name nicely
                name = item.name.replace('-', '/').lstrip('/')
                projects.append({
                    'name': name,
                    'path': item.name,
                    'sessions': sessions
                })

        self.send_json(projects)

    def send_session(self, project_path, session_file):
        if not project_path or not session_file:
            self.send_json({'error': 'Missing project or file'}, 400)
            return

        file_path = PROJECTS_DIR / project_path / session_file
        if not file_path.exists() or not file_path.is_file():
            self.send_json({'error': 'File not found'}, 404)
            return

        messages = []
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line:
                        try:
                            messages.append(json.loads(line))
                        except json.JSONDecodeError:
                            pass
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
            return

        self.send_json(messages)

    def send_json(self, data, status=200):
        content = json.dumps(data).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', len(content))
        self.end_headers()
        self.wfile.write(content)

    def log_message(self, format, *args):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")

def main():
    port = 8765
    server = http.server.HTTPServer(('localhost', port), ProjectsHandler)
    print(f"Claude Threads Viewer running at http://localhost:{port}")
    print("Press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()

if __name__ == '__main__':
    main()
