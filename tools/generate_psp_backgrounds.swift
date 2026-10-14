import AppKit
import Foundation

let themes: [(String, String, NSColor)] = [
    ("cps1", "CPS1", NSColor(calibratedRed: 0.15, green: 0.63, blue: 0.97, alpha: 1)),
    ("cps2", "CPS2", NSColor(calibratedRed: 0.97, green: 0.30, blue: 0.29, alpha: 1)),
    ("mvs", "MVS", NSColor(calibratedRed: 0.27, green: 0.89, blue: 0.59, alpha: 1)),
    ("ncdz", "NCDZ", NSColor(calibratedRed: 0.70, green: 0.49, blue: 0.97, alpha: 1))
]
let width = 480
let height = 272
let output = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "data/psp", isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

for (slug, label, accent) in themes {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    let canvas = NSRect(x: 0, y: 0, width: width, height: height)
    NSColor(calibratedRed: 0.035, green: 0.055, blue: 0.10, alpha: 1).setFill()
    canvas.fill()
    let ctx = NSGraphicsContext.current!.cgContext
    ctx.setLineWidth(0.7)
    for i in 0...22 {
        let x = CGFloat(i * 26 - 70)
        ctx.move(to: CGPoint(x: x, y: 0))
        ctx.addLine(to: CGPoint(x: x + 140, y: 272))
    }
    ctx.setStrokeColor(accent.withAlphaComponent(0.14).cgColor)
    ctx.strokePath()
    for i in 0...13 {
        let y = CGFloat(i * 22)
        ctx.move(to: CGPoint(x: 0, y: y))
        ctx.addLine(to: CGPoint(x: 480, y: y))
    }
    ctx.setStrokeColor(accent.withAlphaComponent(0.09).cgColor)
    ctx.strokePath()
    accent.setFill()
    NSRect(x: 0, y: 0, width: 7, height: 272).fill()
    NSRect(x: 0, y: 248, width: 480, height: 3).fill()
    let orbitCenter = CGPoint(x: 375, y: 137)
    for radius in [CGFloat(61), 86, 111] {
        ctx.addEllipse(in: CGRect(x: orbitCenter.x - radius, y: orbitCenter.y - radius, width: radius * 2, height: radius * 2))
        ctx.setStrokeColor(accent.withAlphaComponent(0.22).cgColor)
        ctx.setLineWidth(1.4)
        ctx.strokePath()
    }
    let font = NSFont.systemFont(ofSize: 54, weight: .black)
    let titleStyle: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.white,
        .kern: 1.0]
    ("NJEMU" as NSString).draw(at: CGPoint(x: 30, y: 144), withAttributes: titleStyle)
    let secondary: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 19, weight: .bold),
        .foregroundColor: accent, .kern: 3.0]
    (label as NSString).draw(at: CGPoint(x: 33, y: 108), withAttributes: secondary)
    let caption: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedSystemFont(ofSize: 10, weight: .medium),
        .foregroundColor: NSColor(calibratedWhite: 0.75, alpha: 1), .kern: 1.4]
    ("ARCADE EMULATION PROJECT" as NSString).draw(at: CGPoint(x: 33, y: 85), withAttributes: caption)
    ("OPEN SOURCE  /  MULTI-PLATFORM" as NSString).draw(at: CGPoint(x: 33, y: 29), withAttributes: caption)
    NSGraphicsContext.restoreGraphicsState()
    guard let data = bitmap.representation(using: .png, properties: [:]) else { fatalError("PNG encode failed") }
    try data.write(to: output.appendingPathComponent("\(slug)_pic1.png"))
    print("\(slug): \(data.count) bytes, \(width)x\(height)")
}
