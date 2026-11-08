package org.xserver.android;

import java.io.IOException;
import java.util.function.BooleanSupplier;

/** UI geometry plus the private native frame-graph capture proof. */
final class MdoBrowserNativeCapture implements AutoCloseable {
    interface Binding {MdoBrowserCdpCapture.Session open(MdoBrowserCdpCapture.Current current)throws IOException;}
    final MdoBrowserCapture.Plan plan;final String captureKey,nativeDocument,document;
    final long navigation;final double reportedScale;
    private final MdoBrowserCaptureTarget target;private final BooleanSupplier owned;
    private final MdoBrowserCdpCapture.Session session;
    MdoBrowserNativeCapture(MdoBrowserCdpCapture operation,MdoBrowserCaptureTarget target,BooleanSupplier owned,Binding binding)throws IOException {
        this.target=target;this.owned=owned;
        captureKey=operation.captureKey;nativeDocument=operation.nativeDocument;document=operation.document;navigation=operation.navigation;
        plan=operation.plan(target.snapshot());reportedScale=operation.reportedScale(plan);
        session=binding.open(this::current);
    }
    private String current(){return owned.getAsBoolean()&&plan.snapshot.same(target.snapshot())?null:"capture_changed";}
    void check(long remaining,MdoBrowserCapture.Checked complete){session.check(remaining,complete);}
    @Override public void close(){session.close();}
}
