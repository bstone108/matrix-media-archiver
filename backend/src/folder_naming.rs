use std::collections::HashSet;

pub fn sanitize_label(label: &str) -> String {
    let invalid = ['/', ':', '\\', '?', '%', '*', '|', '"', '<', '>'];
    let mut value = label
        .chars()
        .map(|ch| if invalid.contains(&ch) { '_' } else { ch })
        .collect::<String>()
        .trim_matches(|ch: char| ch == ' ' || ch == '.')
        .to_owned();

    while value.contains("__") {
        value = value.replace("__", "_");
    }

    if value.is_empty() {
        value = "_".to_owned();
    }

    if value.chars().count() > 120 {
        value = value.chars().take(120).collect();
    }

    value
}

pub fn preferred_label(
    display_name: Option<&str>,
    canonical_alias: Option<&str>,
    remembered_aliases: &[String],
    room_id: &str,
    existing_labels: &HashSet<String>,
) -> String {
    let mut candidates = Vec::new();
    if let Some(value) = display_name {
        candidates.push(value.to_owned());
    }
    if let Some(value) = canonical_alias {
        candidates.push(value.to_owned());
    }
    candidates.extend(remembered_aliases.iter().cloned());

    for candidate in candidates {
        let sanitized = sanitize_label(&candidate);
        if !existing_labels.contains(&sanitized.to_lowercase()) {
            return sanitized;
        }
    }

    sanitize_label(room_id)
}
