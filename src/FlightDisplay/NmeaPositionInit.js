.pragma library

// $PAPPOS,hhmmss.ss,PX,PY,PZ,H_acc,V_acc; checksum is added by the transport.
function sentenceFromFields(fields) {
    if (fields.length !== 6) return ""
    const cleaned = fields.map(function(field) { return field.trim() })
    for (let i = 0; i < cleaned.length; ++i) {
        if (/[^\x20-\x7e]|[,$*!]/.test(cleaned[i])) return ""
    }
    const decimal = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/
    // UTC is optional, but its empty comma-separated position must be retained.
    if (cleaned[0].length && !/^(?:[01]\d|2[0-3])[0-5]\d[0-5]\d(?:\.\d+)?$/.test(cleaned[0])) return ""
    for (let i = 1; i < cleaned.length; ++i) {
        if (!decimal.test(cleaned[i]) || !isFinite(Number(cleaned[i]))) return ""
    }
    if (Number(cleaned[4]) < 0 || Number(cleaned[5]) < 0) return ""
    const latitude = Number(cleaned[1])
    const longitude = Number(cleaned[2])
    if (!isFinite(latitude) || !isFinite(longitude) ||
            latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) return ""
    return "$PAPPOS," + cleaned.join(",")
}
