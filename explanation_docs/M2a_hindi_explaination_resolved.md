# 📚 M2-A: AuroraLink Protocol — पूरी समझ (Hindi में)
### Abhishek ke liye — Step by Step, Story ke Saath

---

## 🎭 पहले एक कहानी — "राजू का Power Plant"

> **कल्पना करो:** एक बड़ा Power Plant है जिसमें दो कमरे हैं।
>
> - **कमरा 1 — RTDS Simulator (बड़ा Computer):** यहाँ एक scientist बैठा है जो बिजली के circuits को simulate करता है। वो हर millisecond में नए readings भेजता है — जैसे voltage, current, commands।
>
> - **कमरा 2 — Kria Board (हमारा FPGA+Processor):** यहाँ एक smart controller है जो उन readings को लेकर real decisions लेता है — जैसे switches on/off करना।
>
> **Problem:** ये दोनों कमरे अलग-अलग हैं। इनके बीच data कैसे जाएगा?
> **Solution:** एक special fiber optic cable — जिसका नाम है **AuroraLink** 🌟

---

## 📦 Part 1: AuroraLink क्या है? (End-to-End समझो)

### 🔑 Key Concept: तीन "खिलाड़ी" हैं