import os
import tempfile

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel
from groq import Groq
from starlette.background import BackgroundTask
from pathlib import Path


# =====================================================
# ENVIRONMENT VARIABLES
# =====================================================

load_dotenv()

GROQ_API_KEY = os.getenv("GROQ_API_KEY")

if not GROQ_API_KEY:
    raise RuntimeError(
        "GROQ_API_KEY is missing from .env"
    )


# =====================================================
# GROQ CLIENT
# =====================================================

client = Groq(
    api_key=GROQ_API_KEY
)


# =====================================================
# FASTAPI APP
# =====================================================

app = FastAPI(
    title="ESP32 Voice Assistant Backend",
    version="3.0"
)

# =====================================================
# PROJECT PATHS
# =====================================================

PROJECT_ROOT = Path(__file__).resolve().parent.parent

TEST_AUDIO_PATH = PROJECT_ROOT / "test_voice.wav"

# =====================================================
# REQUEST MODELS
# =====================================================

class ChatRequest(BaseModel):
    message: str


class TTSRequest(BaseModel):
    text: str


# =====================================================
# ROOT
# =====================================================

@app.get("/")
def root():

    return {
        "message": "ESP32 Voice Assistant Backend"
    }


# =====================================================
# HEALTH CHECK
# =====================================================

@app.get("/health")
def health():

    return {
        "status": "ok",
        "service": "voice-assistant-backend",
        "groq": "configured",
        "stt": "available",
        "chat": "available",
        "tts": "available"
    }


# =====================================================
# CHAT ENDPOINT
# =====================================================

@app.post("/chat")
def chat(request: ChatRequest):

    user_message = request.message.strip()

    if not user_message:

        raise HTTPException(
            status_code=400,
            detail="Message cannot be empty"
        )

    print()
    print("--------------------------------")
    print("[USER]")
    print(user_message)

    try:

        completion = client.chat.completions.create(

            model="openai/gpt-oss-20b",

            messages=[
                {
                    "role": "system",
                    "content": (
                        "You are a voice assistant running through "
                        "an ESP32-S3 device. "
                        "Respond naturally and clearly. "
                        "Keep every answer under 180 characters. "
                        "Use one short sentence whenever possible. "
                        "Avoid markdown. "
                        "Use simple ASCII punctuation where possible."
                    )
                },

                {
                    "role": "user",
                    "content": user_message
                }
            ]
        )

        response_text = (
            completion
            .choices[0]
            .message
            .content
        )

        print()
        print("[ASSISTANT]")
        print(response_text)
        print("--------------------------------")

        return {
            "status": "ok",
            "response": response_text
        }

    except Exception as error:

        print()
        print("[GROQ ERROR]")
        print(error)

        raise HTTPException(
            status_code=500,
            detail="Groq request failed"
        )


# =====================================================
# TEXT TO SPEECH ENDPOINT
# =====================================================
@app.post("/stt")
async def speech_to_text(request: Request):

    audio_bytes = await request.body()

    if not audio_bytes:
        raise HTTPException(
            status_code=400,
            detail="No audio data received"
        )

    print()
    print("--------------------------------")
    print("[STT] Audio received")
    print(f"[STT] Size: {len(audio_bytes)} bytes")

    temp_file = tempfile.NamedTemporaryFile(
        delete=False,
        suffix=".wav"
    )

    temp_path = temp_file.name

    try:

        temp_file.write(audio_bytes)
        temp_file.close()

        with open(temp_path, "rb") as audio_file:

            transcription = client.audio.transcriptions.create(
                file=audio_file,
                model="whisper-large-v3-turbo",
                response_format="json",
                language="en"
            )

        text = transcription.text.strip()

        print("[STT] Transcript:")
        print(text)
        print("--------------------------------")

        return {
            "status": "ok",
            "text": text
        }

    except Exception as error:

        print()
        print("[STT ERROR]")
        print(error)

        raise HTTPException(
            status_code=500,
            detail="Speech-to-text failed"
        )

    finally:

        if os.path.exists(temp_path):
            os.remove(temp_path)
            
@app.post("/tts")
def text_to_speech(request: TTSRequest):

    text = request.text.strip()

    if not text:

        raise HTTPException(
            status_code=400,
            detail="Text cannot be empty"
        )

    # Current Orpheus limit
    if len(text) > 200:

        raise HTTPException(
            status_code=400,
            detail="TTS text must be 200 characters or fewer"
        )

    print()
    print("--------------------------------")
    print("[TTS] Generating speech:")
    print(text)

    try:

        response = client.audio.speech.create(

            model="canopylabs/orpheus-v1-english",

            voice="troy",

            input=text,

            response_format="wav"
        )

        # ---------------------------------------------
        # CREATE TEMPORARY WAV FILE
        # ---------------------------------------------

        temp_file = tempfile.NamedTemporaryFile(
            delete=False,
            suffix=".wav"
        )

        temp_path = temp_file.name

        temp_file.close()

        # ---------------------------------------------
        # SAVE GROQ AUDIO
        # ---------------------------------------------

        response.write_to_file(
            temp_path
        )

        file_size = os.path.getsize(
            temp_path
        )

        print(
            f"[TTS] WAV generated: {file_size} bytes"
        )

        print(
            "[TTS] Generation successful"
        )

        print("--------------------------------")

        # ---------------------------------------------
        # RETURN WAV FILE
        # ---------------------------------------------

        return FileResponse(

            path=temp_path,

            media_type="audio/wav",

            filename="response.wav",

            background=BackgroundTask(
                os.remove,
                temp_path
            )
        )

    except Exception as error:

        print()
        print("[TTS ERROR]")
        print(error)

        raise HTTPException(
            status_code=500,
            detail="TTS generation failed"
        )
        
# =====================================================
# SIMULATED MICROPHONE AUDIO
# =====================================================

@app.get("/test-audio")
def test_audio():

    if not TEST_AUDIO_PATH.exists():

        raise HTTPException(
            status_code=404,
            detail="test_voice.wav not found"
        )

    print()
    print("--------------------------------")
    print("[TEST AUDIO] Sending simulated microphone WAV")
    print(f"[TEST AUDIO] File: {TEST_AUDIO_PATH}")
    print(
        f"[TEST AUDIO] Size: "
        f"{TEST_AUDIO_PATH.stat().st_size} bytes"
    )
    print("--------------------------------")

    return FileResponse(
        path=str(TEST_AUDIO_PATH),
        media_type="audio/wav",
        filename="test_voice.wav"
    )