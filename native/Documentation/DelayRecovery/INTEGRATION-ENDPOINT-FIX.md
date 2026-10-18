# Recovered feedback endpoint correction

The recovered adapter multiplied percentage feedback by .01. In double precision, 95*.01 exceeds the kernel maximumFeedback by one ULP. Three conversions now divide by 100 consistently (captured tail, prepare, process). The kernel remains unchanged.

The unchanged original DelayCoreTests passed with this fix in isolation. A new additive regression covers 2 kinds × 5 rates × 3 delay times: 95% accepted and values above 95% rejected. Integration gates are pending; this is not a Windows claim.
