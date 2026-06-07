import os
import anthropic
from flask import Flask, request, jsonify

app = Flask(__name__)
client = anthropic.Anthropic(api_key=os.environ["ANTHROPIC_API_KEY"])

# Keep last 10 turns of conversation history per session
history = []

DEFAULT_SYSTEM = (
    "You are a helpful assistant on a tiny handheld device. "
    "Keep replies SHORT — under 60 words, plain text, no markdown."
)

MODELS = {
    "sonnet": "claude-sonnet-4-6",
    "opus":   "claude-opus-4-5",
    "haiku":  "claude-haiku-4-5-20251001",
}

current_model = "sonnet"

@app.route("/model", methods=["POST"])
def set_model():
    global current_model
    name = request.get_json(force=True).get("model", "").lower()
    if name in MODELS:
        current_model = name
        return jsonify({"ok": True, "model": name})
    return jsonify({"ok": False, "error": "unknown model"}), 400

@app.route("/chat", methods=["POST"])
def chat():
    data = request.get_json(force=True)
    user_msg = data.get("message", "")
    system   = data.get("system", DEFAULT_SYSTEM) or DEFAULT_SYSTEM

    history.append({"role": "user", "content": user_msg})

    response = client.messages.create(
        model=MODELS[current_model],
        max_tokens=256,
        system=system,
        messages=history,
    )

    reply = response.content[0].text
    history.append({"role": "assistant", "content": reply})

    # Cap history to last 10 turns to avoid ballooning memory
    if len(history) > 20:
        history[:] = history[-20:]

    return jsonify({"reply": reply})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
