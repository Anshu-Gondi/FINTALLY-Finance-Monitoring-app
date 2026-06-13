from datetime import datetime, timezone
from bson import ObjectId
from services.db import budgets
import os
import logging
from pydrive2.auth import GoogleAuth
from pydrive2.drive import GoogleDrive

UTC = timezone.utc

logger = logging.getLogger("fintally.utils.gdrive")

def download_llm_docs_from_drive(target_local_dir: str = "./trusted_docs_source") -> list[str]:
    """
    Authenticates with Google Drive using the root client_secret.json,
    and downloads new files from the 'Llm docs' directory.
    """
    os.makedirs(target_local_dir, exist_ok=True)
    
    gauth = GoogleAuth()
    
    # ── FORCE PYDRIVE2 TO LOOK FOR YOUR EXACT FILENAME ──
    gauth.LoadClientConfigFile("client_secret.json")
    
    # Cache authentication so it only asks for browser permissions once
    if os.path.exists("mycreds.txt"):
        gauth.LoadCredentialsFile("mycreds.txt")
        
    if not gauth.credentials:
        gauth.LocalWebserverAuth()  # Opens browser link on the first build
        gauth.SaveCredentialsFile("mycreds.txt")
    elif gauth.access_token_expired:
        gauth.Refresh()
    else:
        gauth.Authorize()

    drive = GoogleDrive(gauth)
    
    # Query for your Indian Government Tax Documents folder
    query = "title contains 'Llm docs' and mimeType = 'application/vnd.google-apps.folder' and trashed = false"
    folder_list = drive.ListFile({'q': query}).GetList()
    
    if not folder_list:
        logger.error("❌ Google Drive folder 'Llm docs' not found!")
        return []
        
    folder_id = folder_list[0]['id']
    file_list = drive.ListFile({'q': f"'{folder_id}' in parents and trashed = false"}).GetList()
    downloaded_paths = []

    for file_obj in file_list:
        filename = file_obj['title']
        local_file_path = os.path.join(target_local_dir, filename)
        
        # Avoid computational/network waste: if it's already on your drive, skip
        if os.path.exists(local_file_path):
            continue
            
        logger.info(f"Downloading new source file from Google Drive: {filename}")
        file_obj.GetContentFile(local_file_path)
        downloaded_paths.append(local_file_path)
        
    return downloaded_paths

def serialize_datetime(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.isoformat().replace("+00:00", "Z")


def parse_date(date_str: str):
    if not date_str:
        return None
    try:
        return datetime.fromisoformat(date_str.replace("Z", "+00:00"))
    except Exception:
        return None


async def get_active_budget(user_id: str):
    now = datetime.utcnow().replace(tzinfo=UTC)
    return await budgets.find_one(
        {
            "userId": ObjectId(user_id),
            "startDate": {"$lte": now},
            "$or": [
                {"endDate": None},
                {"endDate": {"$gte": now}},
            ],
        },
        sort=[("startDate", -1)],
    )