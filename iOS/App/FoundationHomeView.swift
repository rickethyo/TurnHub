import SwiftUI
import TurnHubCore

/// Development shell; playable screens replace this as the milestones land.
struct FoundationHomeView: View {
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("TurnHub for iPhone and iPad is in development.")
                        .font(.headline)
                    Text("Play on your device, or bring everyone together with Atlas.")
                        .foregroundStyle(.secondary)
                }

                Section("Coming to TurnHub") {
                    feature("Play on this device",
                            detail: "A standalone table, without an account or internet.")
                    feature("Players and history",
                            detail: "Keep your local players and finished games.")
                    feature("Connect to Atlas",
                            detail: "Join a shared table with phones and Sigils.")
                }

                Section("Development status") {
                    Text("App foundation only. Gameplay and Atlas connection are coming next.")
                    Text("Atlas HTTP API \(AtlasCompatibility.apiVersion)")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("TurnHub")
        }
    }

    private func feature(_ title: String, detail: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.headline)
            Text(detail).foregroundStyle(.secondary)
        }
        .padding(.vertical, 4)
        .accessibilityElement(children: .combine)
    }
}

#Preview {
    FoundationHomeView()
}
