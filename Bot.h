#pragma once

namespace Bot {
    void Start();    // Worker thread başlat
    void Stop();     // Durdur
    void Update();   // Present'dan çağrılır (sadece debug print)
}
