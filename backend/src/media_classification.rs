use mime_guess::MimeGuess;

use crate::domain::MediaCategory;

const IMAGE_EXTENSIONS: &[&str] = &[
    "jpg", "jpeg", "png", "gif", "webp", "avif", "heic", "heif", "jxl", "bmp", "tif", "tiff",
    "ico", "icns",
];
const VIDEO_EXTENSIONS: &[&str] = &[
    "mp4", "m4v", "mov", "webm", "mkv", "avi", "mpg", "mpeg", "ts", "mts", "m2ts", "flv", "ogv",
    "3gp", "3g2", "wmv", "asf", "mxf",
];
const PROGRAM_EXTENSIONS: &[&str] = &[
    "dmg", "pkg", "app", "ipa", "apk", "exe", "msi", "deb", "rpm", "appimage", "jar", "bin", "run",
    "command", "bat", "ps1",
];
const ARCHIVE_EXTENSIONS: &[&str] = &["zip", "tar", "gz", "tgz", "bz2", "xz", "7z", "rar", "zst"];
const DOCUMENT_EXTENSIONS: &[&str] = &[
    "pdf", "txt", "md", "rtf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp",
    "csv", "json", "xml", "yaml", "yml",
];

pub fn category(filename: Option<&str>, mime_type: Option<&str>) -> MediaCategory {
    if let Some(ext) = filename_extension(filename) {
        if IMAGE_EXTENSIONS.contains(&ext.as_str()) {
            return MediaCategory::Images;
        }
        if VIDEO_EXTENSIONS.contains(&ext.as_str()) {
            return MediaCategory::Videos;
        }
        if PROGRAM_EXTENSIONS.contains(&ext.as_str()) {
            return MediaCategory::Programs;
        }
        if ARCHIVE_EXTENSIONS.contains(&ext.as_str()) {
            return MediaCategory::Archives;
        }
        if DOCUMENT_EXTENSIONS.contains(&ext.as_str()) {
            return MediaCategory::Documents;
        }

        if let Some(mime) = MimeGuess::from_ext(&ext).first() {
            if mime.type_() == mime::IMAGE {
                return MediaCategory::Images;
            }
            if mime.type_() == mime::VIDEO {
                return MediaCategory::Videos;
            }
            if mime.type_() == mime::AUDIO {
                return MediaCategory::Audio;
            }
        }
    }

    if let Some(mime) = normalized_mime_type(mime_type) {
        if mime.starts_with("image/") {
            return MediaCategory::Images;
        }
        if mime.starts_with("video/") {
            return MediaCategory::Videos;
        }
        if mime.starts_with("audio/") {
            return MediaCategory::Audio;
        }
        if mime == "application/webm" || mime == "application/x-matroska" {
            return MediaCategory::Videos;
        }
        if mime == "application/pdf" || mime.starts_with("text/") {
            return MediaCategory::Documents;
        }
        if mime.contains("zip") || mime.contains("compressed") || mime.contains("archive") {
            return MediaCategory::Archives;
        }
        if mime.contains("msi")
            || mime.contains("executable")
            || mime.contains("application/x-dosexec")
        {
            return MediaCategory::Programs;
        }
    }

    MediaCategory::Other
}

pub fn preferred_extension(filename: Option<&str>, mime_type: Option<&str>) -> Option<String> {
    if let Some(ext) = filename_extension(filename) {
        return Some(ext);
    }

    let mime = normalized_mime_type(mime_type)?;
    match mime.as_str() {
        "image/jpeg" => Some("jpg".to_owned()),
        "image/png" => Some("png".to_owned()),
        "image/gif" => Some("gif".to_owned()),
        "image/webp" => Some("webp".to_owned()),
        "image/avif" => Some("avif".to_owned()),
        "image/heic" => Some("heic".to_owned()),
        "image/heif" => Some("heif".to_owned()),
        "image/jxl" => Some("jxl".to_owned()),
        "image/bmp" => Some("bmp".to_owned()),
        "image/tiff" => Some("tiff".to_owned()),
        "video/mp4" | "application/mp4" => Some("mp4".to_owned()),
        "video/quicktime" => Some("mov".to_owned()),
        "video/webm" | "application/webm" => Some("webm".to_owned()),
        "video/x-matroska" | "application/x-matroska" => Some("mkv".to_owned()),
        "video/x-msvideo" => Some("avi".to_owned()),
        "video/x-ms-wmv" => Some("wmv".to_owned()),
        "video/mpeg" => Some("mpeg".to_owned()),
        "video/3gpp" => Some("3gp".to_owned()),
        "video/3gpp2" => Some("3g2".to_owned()),
        _ => mime_guess::get_mime_extensions_str(&mime)
            .and_then(|extensions| extensions.first().copied())
            .map(str::to_lowercase),
    }
}

pub fn filename_extension(filename: Option<&str>) -> Option<String> {
    let filename = filename?;
    let ext = std::path::Path::new(filename)
        .extension()?
        .to_str()?
        .trim()
        .to_lowercase();
    if ext.is_empty() { None } else { Some(ext) }
}

fn normalized_mime_type(mime_type: Option<&str>) -> Option<String> {
    let mime_type = mime_type?;
    let normalized = mime_type.split(';').next()?.trim().to_lowercase();
    if normalized.is_empty() {
        None
    } else {
        Some(normalized)
    }
}
