"""SmartGlass HUD v0.4 LAN bridge (development prototype).

Run: pip install fastapi uvicorn
     python -m uvicorn server.app:app --host 0.0.0.0 --port 8765

POST /api/messages with {"type":"agent.text","text":"Hello"}
ESP32 connects to ws://<LAN-IP>:8765/ws/hud?token=<TOKEN>.
Do not expose this development server to the public internet.
"""
import asyncio
import os
import secrets
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Header, HTTPException
from pydantic import BaseModel, Field

TOKEN = os.getenv("SGHUD_TOKEN", "")
if not TOKEN:
    raise RuntimeError("Set SGHUD_TOKEN to a long random secret before starting")

app = FastAPI(title="SmartGlass HUD v0.4 Bridge")
clients: set[WebSocket] = set()
lock = asyncio.Lock()

class Message(BaseModel):
    id: str = Field(min_length=1, max_length=80)
    type: str = Field(pattern=r"^(agent\\.text|agent\\.status|hud\\.notification|hud\\.clear)$")
    text: str = Field(default="", max_length=512)
    seq: int = Field(default=0, ge=0)
    final: bool = True

@app.get("/health")
async def health():
    return {"status": "ok", "clients": len(clients)}

@app.websocket("/ws/hud")
async def hud(ws: WebSocket):
    supplied = ws.query_params.get("token", "")
    if not secrets.compare_digest(supplied, TOKEN):
        await ws.close(code=1008)
        return
    await ws.accept()
    async with lock:
        clients.add(ws)
    try:
        while True:
            await ws.receive_text()  # heartbeat / client acknowledgements
    except WebSocketDisconnect:
        pass
    finally:
        async with lock:
            clients.discard(ws)

@app.post("/api/messages")
async def send(message: Message, authorization: str | None = Header(default=None)):
    if not secrets.compare_digest(authorization or "", "Bearer " + TOKEN):
        raise HTTPException(status_code=401, detail="Unauthorized")
    payload = {"v": 1, **message.model_dump()}
    async with lock:
        targets = list(clients)
    delivered = 0
    for ws in targets:
        try:
            await ws.send_json(payload)
            delivered += 1
        except Exception:
            async with lock:
                clients.discard(ws)
    return {"accepted": True, "delivered": delivered}
