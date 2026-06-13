module;

export module quader.app.editor_input_state;

export namespace quader {

class IEditorInputState {
public:
    virtual ~IEditorInputState() = default;

    [[nodiscard]] virtual bool text_input_active() const = 0;
};

} // namespace quader
