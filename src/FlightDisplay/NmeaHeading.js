.pragma library

// $PAPRPH,hhmmss.ss,R,P,Y,R_acc,P_acc,H_acc; checksum is added by the transport.
function sentenceFromFields(fields) {
    if (fields.length !== 7) return ""
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
    if (Number(cleaned[4]) < 0 || Number(cleaned[5]) < 0 || Number(cleaned[6]) < 0) return ""
    const heading = Number(cleaned[3])
    if (heading < 0 || heading > 360) return ""
    return "$PAPRPH," + cleaned.join(",")
}
