#include "dmadac.h"

#include <Adafruit_ZeroDMA.h>
#include <wiring_private.h> // for pinPeripheral()


namespace {
  class ZeroSource : public SoundSource {
  public:
    virtual void supply(sample_t* buffer, int count) {
      while (count--)
        *buffer++ = SAMPLE_ZERO;
    }
  };

  ZeroSource _zeroSource;

  class TestRampSource : public SoundSource {
  public:
    virtual void supply(sample_t* buffer, int count) {
      sample_t bump = SAMPLE_UNIT / count;
      sample_t s = SAMPLE_ZERO;
      if (even) for (; count--; s += bump) *buffer++ = s;
      else      for (; count--; s -= bump) *buffer++ = s;
      even = !even;
    }
  private:
    bool even = true;
  };

  TestRampSource _testRampSource;
}

SoundSource& zeroSource = _zeroSource;

namespace {

  using dac_t = uint16_t;

  constexpr int DAC_BITS = 10;           // DAC on SAM D21 is only 10 bits
  constexpr dac_t DAC_ZERO = 1 << (DAC_BITS - 1);
  constexpr dac_t DAC_UNIT = DAC_ZERO - 1;
  constexpr dac_t DAC_POS_ONE = DAC_ZERO + DAC_UNIT;
  constexpr dac_t DAC_NEG_ONE = DAC_ZERO - DAC_UNIT;

  SoundSource* dmaSource = &zeroSource;

  Adafruit_ZeroDMA dma;

  const int buffer_count = 96;
  sample_t buffer_a[buffer_count];
  sample_t buffer_b[buffer_count];
  bool transferring_buffer_a;

  void fillBuffer(sample_t* b) {
    dmaSource->supply(b, buffer_count);
  }

  volatile unsigned int dmaCount = 0;
  volatile unsigned long dmaTime = 0;

  void dmaDoneCallback(Adafruit_ZeroDMA* _dma) {
    if (_dma != &dma) return;
    dmaCount += 1;
    auto t0 = micros();

    sample_t* buf = transferring_buffer_a ? buffer_a : buffer_b;
    transferring_buffer_a = !transferring_buffer_a;
    fillBuffer(buf);

    static_assert(sizeof(dac_t) == sizeof(sample_t),
      "dac_t and sample_t not the same size");
      // because a buffer of samples is converted into a buffer of dac values

    using sdac_t = SFixed<15, 16>;
    constexpr sdac_t SDAC_MAX = sdac_t(DAC_UNIT) >> (DAC_BITS - 1);
    constexpr sdac_t SDAC_MIN = - SDAC_MAX;

    for (int i = buffer_count; i; --i, ++buf) {
      sdac_t u(*buf);
      dac_t v;
      if (u > SDAC_MAX)       v = DAC_POS_ONE;
      else if (u < SDAC_MIN)  v = DAC_NEG_ONE;
      else
        v = dac_t((u << (DAC_BITS - 1)) + sdac_t(DAC_ZERO));
      *((dac_t*)buf) = v;
    }

    auto t1 = micros();
    dmaTime += t1 - t0;   // should still work if it rolls over!
  }
}

namespace DmaDac {
  void setSource(SoundSource& s) { dmaSource = &s; }

  void begin() {

    // TIMER INIT ------------------------------------------------------------
    // TC4 is used because it has a WO[] output mappable to a pin on the CPE

    pinPeripheral(A7, PIO_TIMER);

    GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 |
                                  GCLK_CLKCTRL_ID(GCM_TC4_TC5));
    while (GCLK->STATUS.bit.SYNCBUSY == 1)
      ;

    TC4->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE; // Disable TCx to config it
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY)
      ;

    TC4->COUNT16.CTRLA.reg =     // Configure timer counter
        TC_CTRLA_MODE_COUNT16 |  // 16-bit counter mode
        TC_CTRLA_WAVEGEN_MFRQ |  // Match Frequency mode
        TC_CTRLA_PRESCALER_DIV1; // 1:1 Prescale
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY)
      ;

    TC4->COUNT16.CC[0].reg = SAMPLE_RATE_CPU_DIVISOR - 1;
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY)
      ;

    TC4->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE; // Re-enable TCx
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY)
      ;

    // DAC INIT --------------------------------------------------------------

#ifdef ADAFRUIT_CIRCUITPLAYGROUND_M0
    // pinMode(11, OUTPUT);
    // digitalWrite(11, LOW); // Switch off speaker (DAC to A0 pin only)
#endif
    analogWriteResolution(DAC_BITS); // Let Arduino core initialize the DAC,
    analogWrite(A0, DAC_ZERO);       // ain't nobody got time for that!
    DAC->CTRLB.bit.REFSEL = 0;          // VMAX = 1.0V
    while (DAC->STATUS.bit.SYNCBUSY)
      ;

    // DMA INIT --------------------------------------------------------------

    fillBuffer(buffer_a);
    fillBuffer(buffer_b);
    transferring_buffer_a = true;


    dma.allocate();
    dma.setTrigger(TC4_DMAC_ID_OVF);
    dma.setAction(DMA_TRIGGER_ACTON_BEAT);
    dma.setPriority(DMA_PRIORITY_3);    // highest priority for DMAC

    NVIC_SetPriority(DMAC_IRQn, 0);     // highest priority for NVIC
    NVIC_SetPriority(PTC_IRQn, 1);      // make sure that PTC is lower
      // must be done after Adafruit_ZeroDMA::allocate(), which sets it to 3
    USB->DEVICE.QOSCTRL.bit.CQOS = 2;
    USB->DEVICE.QOSCTRL.bit.DQOS = 2;
    DMAC->QOSCTRL.bit.DQOS = 3;
    DMAC->QOSCTRL.bit.FQOS = 3;
    DMAC->QOSCTRL.bit.WRBQOS = 3;

    auto desc1 = dma.addDescriptor(
      buffer_a,
      (void *)&DAC->DATABUF.reg,
      buffer_count,
      DMA_BEAT_SIZE_HWORD,
      true,
      false
    );
    desc1->BTCTRL.bit.BLOCKACT = DMA_BLOCK_ACTION_INT;

    auto desc2 = dma.addDescriptor(
      buffer_b,
      (void *)&DAC->DATABUF.reg,
      buffer_count,
      DMA_BEAT_SIZE_HWORD,
      true,
      false
    );
    desc2->BTCTRL.bit.BLOCKACT = DMA_BLOCK_ACTION_INT;

    dma.loop(true);
    dma.setCallback(dmaDoneCallback);
    dma.startJob();
  }

  void report(Print& out) {
    static unsigned long lastMicros = 0;
    unsigned long m = micros();
    unsigned long t = m - lastMicros;
    lastMicros = m;

    static int lastDmaCount = 0;
    int d = dmaCount;
    int n = d - lastDmaCount;
    lastDmaCount = d;

    static unsigned long lastDmaTime = 0;
    unsigned long u = dmaTime;
    unsigned long v = u - lastDmaTime;
    lastDmaTime = u;

    float sr = float(n * buffer_count) * 1000000.0f / float(t);

    out.printf("DMA to DAC: %d buffers sent in %dus, %dHz\n", n, t, int(sr));
    out.printf("   %dµs filling buffers, %dµs/buffer\n", v, v / n);

#if 0
    sample_t buf[buffer_count];
    memcpy(buf,
      transferring_buffer_a ? buffer_a : buffer_b,
      sizeof(sample_t) * buffer_count);

    for (int i = 0; i < buffer_count; ++i) {
      if (i % 20 == 0) out.printf("   [%2d]", i);
      out.printf(" %5d", buf[i]);
      if (i % 20 == 19) out.println();
    }
    out.println();
#endif
  }
}